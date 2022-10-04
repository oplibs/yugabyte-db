// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import com.yugabyte.util.PSQLException;

@RunWith(value=YBTestRunner.class)
public class TestPgYbStat extends BasePgSQLTest {
  private static final Integer MAX_PG_STAT_RETRIES = 20;
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);
  private static final int YB_TERMINATED_QUERIES_MAX_SIZE = 1000;

  private void executeQueryAndExpectNoResults(final String query,
    final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      statement.executeQuery(String.format(query));
    } catch (PSQLException exception) {
      assertEquals("No results were returned by the query.", exception.getMessage());
    }
  }

  // TODO: Will remove this when blocking upgrade issue lands that prohibits a system
  // view from being properly added moving between different versions.
  private void createQueryTerminationView(final Connection inputConnection) throws Exception {
    String createViewQuery = "CREATE VIEW yb_terminated_queries AS " +
      "SELECT " +
            "D.datname AS databasename," +
            "S.backend_pid AS backend_pid," +
            "S.query_text AS query_text," +
            "S.termination_reason AS termination_reason," +
            "S.query_start AS query_start_time," +
            "S.query_end AS query_end_time " +
      "FROM yb_pg_stat_get_queries(NULL) AS S " +
      "LEFT JOIN pg_database AS D ON (S.db_oid = D.oid);";
    executeQueryAndExpectNoResults(createViewQuery, inputConnection);
  }

  private void executeQueryAndExpectTempFileLimitExceeded(final String query,
    final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      statement.executeQuery(String.format(query));
    } catch (PSQLException exception) {
      assertEquals("ERROR: temporary file size exceeds temp_file_limit (0kB)",
                   exception.getMessage());
    }
  }

  private void setupMinTempFileConfigs(final Connection inputConnection) throws Exception {
    executeQueryAndExpectNoResults("SET work_mem TO 64", inputConnection);
    executeQueryAndExpectNoResults("SET temp_file_limit TO 0", inputConnection);
  }

  private int getPid(final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      ResultSet result = statement.executeQuery(String.format("SELECT pg_backend_pid() AS pid"));
      assertTrue(result.next());
      return result.getInt("pid");
    }
  }

  private int getCurrentDatabaseOid(final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      ResultSet result = statement.executeQuery(
        String.format("SELECT oid FROM pg_database WHERE datname = current_database()"));
      assertTrue(result.next());
      return result.getInt("oid");
    }
  }

  private ArrayList<Connection> createConnections(int numConnections) throws Exception {
    final ArrayList<Connection> connections = new ArrayList<Connection>(numConnections);
    // Create numConnections postgres connections
    for (int i = 0; i < numConnections; ++i) {
      ConnectionBuilder b = getConnectionBuilder();
      b.withTServer(0);
      connections.add(b.connect());
    }
    return connections;
  }

  private interface CheckResultSetInterface {
    public boolean checkResultSet(ResultSet set) throws Exception;
  };

  private boolean waitUntilConditionSatisfiedOrTimeout(String query,
    Connection inputConnection, CheckResultSetInterface checkResultSetCallback) throws Exception {
    int retries = 0;
    while (retries++ < MAX_PG_STAT_RETRIES) {
      try (Statement statement = inputConnection.createStatement()) {
        ResultSet resultSet = statement.executeQuery(String.format(query));
        if (checkResultSetCallback.checkResultSet(resultSet))
          return true;
      }
      Thread.sleep(200);
    }
    return false;
  }

  @Test
  public void testYbTerminatedQueriesOverflow() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();
    setupMinTempFileConfigs(connection);

    final int num_queries = 1006;

    try (Statement statement = connection.createStatement()) {
      for (int i = 0; i < num_queries; i++) {
        final String query = String.format("SELECT * FROM generate_series(0, 1000000 + %d)", i);
        executeQueryAndExpectTempFileLimitExceeded(query, connection);
      }

      executeQueryAndExpectNoResults("SET work_mem TO 2048", connection);

      createQueryTerminationView(connection);

      // By current implementation, we expect that the queries will overflow from the end
      // of the array and start overwiting the oldest entries stored at the beginning of
      // the array. Consider this a circular buffer.
      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT query_text FROM yb_terminated_queries", connection,
        (ResultSet resultSet) -> {
          for (int i = 0; i < YB_TERMINATED_QUERIES_MAX_SIZE; i++) {
            // expected_token = n, n + 1, n + 2, n + 3, n + 4, n + 5, 6, 7, 8, ... n - 1
            // where n = YB_TERMINATED_QUERIES_MAX_SIZE
            int expected_token = i + YB_TERMINATED_QUERIES_MAX_SIZE < num_queries
                ? i + YB_TERMINATED_QUERIES_MAX_SIZE
                : i;
            String expected_query = String.format("SELECT * FROM generate_series(0, 1000000 + %d)",
                                                  expected_token);
            if (!resultSet.next() || !expected_query.equals(resultSet.getString("query_text")))
              return false;
          }
          assertFalse("The size of yb_terminated_queries is greater than the max size",
              resultSet.next());
          return true;
        }));
    }
  }

  @Test
  public void testYBMultipleConnections() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();

    final ArrayList<Connection> connections = createConnections(2);
    final Connection connection1 = connections.get(0);
    final Connection connection2 = connections.get(1);

    setupMinTempFileConfigs(connection1);
    setupMinTempFileConfigs(connection2);

    final String statement1 = "SELECT * FROM generate_series(0, 1000000)";
    final String statement2 = "SELECT * FROM generate_series(6, 1234567)";
    executeQueryAndExpectTempFileLimitExceeded(statement1, connection1);
    executeQueryAndExpectTempFileLimitExceeded(statement2, connection2);

    createQueryTerminationView(connection1);

    assertTrue(waitUntilConditionSatisfiedOrTimeout(
      "SELECT backend_pid, query_text FROM yb_terminated_queries", connection1,
      (ResultSet result) -> {
        if (!result.next()) return false;
        if (!statement1.equals(result.getString("query_text"))
            || getPid(connection1) != result.getInt("backend_pid"))
            return false;
        if (!result.next()) return false;
        if (!statement2.equals(result.getString("query_text")) ||
            getPid(connection2) != result.getInt("backend_pid"))
            return false;
        return true;
      }));
  }

  @Test
  public void testYBDBFiltering() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();

    final String databaseName = "db";
    final String database2Name = "db2";

    executeQueryAndExpectNoResults("CREATE DATABASE " + databaseName, connection);

    final String statement1 = "SELECT * FROM generate_series(0, 1000000)";
    final String statement2 = "SELECT * FROM generate_series(0, 9999999)";
    final String statement3 = "SELECT * FROM generate_series(9, 1234567)";
    final String statement4 = "SELECT * FROM generate_series(80, 1791283)";

    try (Connection connection2 = getConnectionBuilder().withDatabase(databaseName).connect()) {
      setupMinTempFileConfigs(connection2);

      executeQueryAndExpectTempFileLimitExceeded(statement1, connection2);
      executeQueryAndExpectTempFileLimitExceeded(statement2, connection2);
    }

    executeQueryAndExpectNoResults("CREATE DATABASE " + database2Name, connection);

    try (Connection connection2 = getConnectionBuilder().withDatabase(database2Name).connect()) {
      setupMinTempFileConfigs(connection2);

      executeQueryAndExpectTempFileLimitExceeded(statement3, connection2);
      executeQueryAndExpectTempFileLimitExceeded(statement4, connection2);

      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT S.query_text FROM yb_pg_stat_get_queries(" +
        getCurrentDatabaseOid(connection2) + "::Oid) as S", connection2,
        (ResultSet result) -> {
          if (!result.next()) return false;
          if (!statement3.equals(result.getString("query_text"))) return false;
          if (!result.next()) return false;
          if (!statement4.equals(result.getString("query_text"))) return false;
          return !result.next();
        }));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(databaseName).connect()) {
      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT S.query_text FROM yb_pg_stat_get_queries("
        + getCurrentDatabaseOid(connection2) + "::Oid) as S", connection2,
        (ResultSet result) -> {
          if (!result.next()) return false;
          if (!statement1.equals(result.getString("query_text"))) return false;
          if (!result.next()) return false;
          if (!statement2.equals(result.getString("query_text"))) return false;
          return !result.next();
        }));
    }
  }
}
