package org.yb.loadtester.workload;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import org.apache.commons.cli.CommandLine;
import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.Configuration;
import org.yb.loadtester.common.TimeseriesLoadGenerator;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

/**
 * A sample timeseries metric data application.
 *
 * There are NUM_USERS users in the demo system. Each of these users has some number of
 * nodes (which is between MIN_NODES_PER_USER and MAX_NODES_PER_USER) that emit time series data.
 *
 * The usersTable has a list of users, the number of nodes they each have, etc.
 *
 * The metricsTable has the metric timeseries data for all the users' nodes.
 */
public class CassandraTimeseries extends Workload {
  private static final Logger LOG = Logger.getLogger(CassandraTimeseries.class);
  // Static initialization of this workload's config.
  static {
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 1;
    workloadConfig.numWriterThreads = 1;
    // Set the number of keys to read and write.
    workloadConfig.numKeysToRead = -1;
    workloadConfig.numKeysToWrite = -1;
  }

  // The number of users.
  private static int num_users = 100;
  // The minimum number of metrics emitted per data source.
  private static int min_nodes_per_user = 1;
  // The maximum number of metrics emitted per data source.
  private static int max_nodes_per_user = 10;
  // The minimum number of metrics emitted per data source.
  private static int min_metrics_count = 1;
  // The maximum number of metrics emitted per data source.
  private static int max_metrics_count = 1;
  // The rate at which each metric is generated in millis.
  private static long data_emit_rate_millis = 1 * 1000;
  static Random random = new Random();
  // The table that has the raw metric data.
  private String metricsTable = "ts_metrics_raw";
  // The structure to hold all the user info.
  static List<DataSource> dataSources = new CopyOnWriteArrayList<DataSource>();
  // Variable to track if verification is turned off for any datasource.
  private AtomicBoolean verificationDisabled = new AtomicBoolean(false);
  // Max write lag across data sources.
  private AtomicLong maxWriteLag = new AtomicLong(0);

  @Override
  public void initialize(Configuration configuration) {
    synchronized (dataSources) {
      // If the datasources have already been created, we have already initialized the static
      // variables, so nothing to do.
      if (!dataSources.isEmpty()) {
        return;
      }

      // Read the various params from the command line.
      CommandLine commandLine = configuration.getCommandLine();
      if (commandLine.hasOption("num_users")) {
        num_users = Integer.parseInt(commandLine.getOptionValue("num_users"));
      }
      if (commandLine.hasOption("min_nodes_per_user")) {
        min_nodes_per_user = Integer.parseInt(commandLine.getOptionValue("min_nodes_per_user"));
      }
      if (commandLine.hasOption("max_nodes_per_user")) {
        max_nodes_per_user = Integer.parseInt(commandLine.getOptionValue("max_nodes_per_user"));
      }
      if (commandLine.hasOption("min_metrics_count")) {
        min_metrics_count = Integer.parseInt(commandLine.getOptionValue("min_metrics_count"));
      }
      if (commandLine.hasOption("max_metrics_count")) {
        max_metrics_count = Integer.parseInt(commandLine.getOptionValue("max_metrics_count"));
      }
      if (commandLine.hasOption("data_emit_rate_millis")) {
        data_emit_rate_millis =
            Long.parseLong(commandLine.getOptionValue("data_emit_rate_millis"));
      }

      for (int user_idx = 0; user_idx < num_users; user_idx++) {
        // Generate the number of nodes this user would have.
        int num_nodes = min_nodes_per_user +
                        (max_nodes_per_user > min_nodes_per_user ?
                            random.nextInt(max_nodes_per_user - min_nodes_per_user) : 0);
        for (int node_idx = 0; node_idx < num_nodes; node_idx++) {
          // Generate the number of metrics this data source would emit.
          int num_metrics = min_metrics_count +
                            (max_metrics_count > min_metrics_count ?
                                random.nextInt(max_metrics_count - min_metrics_count) : 0);
          // Create the data source.
          DataSource dataSource =
              new DataSource(user_idx, node_idx, data_emit_rate_millis, num_metrics);
          dataSources.add(dataSource);
        }
      }
    }
  }

  @Override
  public String getExampleUsageOptions(String optsPrefix, String optsSuffix) {
    return optsPrefix + "--num_threads_read 2" + optsSuffix +
           optsPrefix + "--num_threads_write 32" + optsSuffix +
           optsPrefix + "--num_users 100" + optsSuffix +
           optsPrefix + "--min_nodes_per_user 5" + optsSuffix +
           optsPrefix + "--max_nodes_per_user 10" + optsSuffix +
           optsPrefix + "--min_metrics_count 10" + optsSuffix +
           optsPrefix + "--max_metrics_count 10" + optsSuffix +
           optsPrefix + "--data_emit_rate_millis 1000" + optsSuffix +
           optsPrefix + "--table_ttl_seconds " + 24 * 60 * 60;
  }

  @Override
  public void dropTable() {
    try {
      String drop_stmt = String.format("DROP TABLE %s;", metricsTable);
      getCassandraClient().execute(drop_stmt);
      LOG.info("Dropped Cassandra table " + metricsTable + " using query: [" + drop_stmt + "]");
    } catch (Exception e) {
      LOG.info("Ignoring exception dropping table: " + e.getMessage());
    }
  }

  @Override
  public void createTableIfNeeded() {
    String create_stmt = "CREATE TABLE " + metricsTable + " (" +
                         "  user_id varchar" +
                         ", metric_id varchar" +
                         ", node_id varchar" +
                         ", ts timestamp" +
                         ", value varchar" +
                         ", primary key ((user_id, metric_id), node_id, ts))";
    if (workloadConfig.tableTTLSeconds > 0) {
      create_stmt += " WITH default_time_to_live = " + workloadConfig.tableTTLSeconds;
    }
    create_stmt += ";";
    getCassandraClient().execute(create_stmt);
    LOG.info("Created a Cassandra table " + metricsTable + " using query: [" + create_stmt + "]");
  }

  @Override
  public long doRead() {
    // Pick a ransom data source.
    DataSource dataSource = dataSources.get(random.nextInt(dataSources.size()));
    // Make sure it has emitted data, otherwise there is nothing to read.
    if (!dataSource.getHasEmittedData()) {
      return 0;
    }
    long startTs = dataSource.getStartTs();
    long endTs = dataSource.getEndTs();
    String select_stmt =
        String.format("SELECT * from %s WHERE user_id='%s'" +
                      " AND metric_id='%s'" +
                      " AND node_id='%s'" +
                      " AND ts>%d AND ts<%d;",
                      metricsTable, dataSource.getUserId(), dataSource.getRandomMetricId(),
                      dataSource.getNodeId(), startTs, endTs);
    ResultSet rs = getCassandraClient().execute(select_stmt);
    List<Row> rows = rs.all();
    // TODO: there is still a verification bug that needs to be tracked down.
    // If the load tester is not able to keep up, data verification will be turned off.
//    int expectedNumDataPoints = dataSource.getExpectedNumDataPoints(startTs, endTs);
//    if (expectedNumDataPoints == -1 && !verificationDisabled.get()) {
//      verificationDisabled.set(true);
//      long writeLag = dataSource.getWriteLag();
//      if (maxWriteLag.get() < writeLag) {
//        maxWriteLag.set(writeLag);
//      }
//    }
//    // If the load tester is able to keep up, we may end up inserting the latest data point a
//    // little after the timestamp it denotes. This causes that data point to expire a little later
//    // than the timestamp it denotes, causing some unpredictability on when the last data point
//    // will expire. To get over this, we allow for a fuzzy match on the number of results
//    // returned.
//    if (expectedNumDataPoints > -1 && Math.abs(rows.size() -  expectedNumDataPoints) > 1) {
//      StringBuilder sb = new StringBuilder();
//      for (Row row : rows) {
//        sb.append(row.toString() + " | ");
//      }
//      LOG.warn("Read " + rows.size() + " data points from DB, expected " +
//               expectedNumDataPoints + " data points, query [" + select_stmt + "], " +
//               "results from DB: { " + sb.toString() + " }, " +
//               "debug info: " + dataSource.printDebugInfo(startTs, endTs));
//    }
    return 1;
  }

  @Override
  public long doWrite() {
    // Pick a random data source.
    DataSource dataSource = dataSources.get(random.nextInt(dataSources.size()));
    // Enter as many data points as are needed.
    long ts = dataSource.getDataEmitTs();
    long numKeysWritten = 0;
    if (ts == -1) {
      try {
        Thread.sleep(100 /* millisecs */);
      } catch (Exception e) {}
      return 0; /* numKeysWritten */
    }
    if (ts > -1) {
      String value = String.format("value-%s", ts);
      for (String metric : dataSource.getMetrics()) {
        String insert_stmt =
            String.format("INSERT INTO %s (user_id, metric_id, node_id, ts, value) VALUES " +
                          "('%s', '%s', '%s', %s, '%s');",
                          metricsTable, dataSource.getUserId(), metric,
                          dataSource.getNodeId(), ts, value);
        ResultSet resultSet = getCassandraClient().execute(insert_stmt);
        numKeysWritten++;
        LOG.debug("Executed query: " + insert_stmt + ", result: " + resultSet.toString());
      }
      dataSource.setLastEmittedTs(ts);
      ts = dataSource.getDataEmitTs();
    }
    return numKeysWritten;
  }

  @Override
  public void appendMessage(StringBuilder sb) {
    super.appendMessage(sb);
    sb.append("Verification: " + (verificationDisabled.get()?"OFF":"ON"));
    if (verificationDisabled.get()) {
      sb.append(" (write lag = " + maxWriteLag.get() + " ms)");
    }
    sb.append(" | ");
  }

  /**
   * This class represents a single data source, which sends back timeseries data for a bunch of
   * metrics. Each data source generates data for all metrics at the same time interval, which is
   * governed by emit rate.
   */
  public static class DataSource extends TimeseriesLoadGenerator {
    // The user id this data source represents.
    String user_id;
    // The node that this data source represents.
    String node_id;
    // The list of metrics to emit for this data source.
    List<String> metrics;
    // The data emit rate.
    long dataEmitRateMs;

    public DataSource(int user_idx, int node_idx, long dataEmitRateMs, int num_metrics) {
      super(user_idx, dataEmitRateMs, workloadConfig.tableTTLSeconds * 1000L);
      this.dataEmitRateMs = dataEmitRateMs;
      this.user_id = super.getId();
      this.node_id = String.format("node-%05d", node_idx);
      metrics = new ArrayList<String>(num_metrics);
      for (int idx = 0; idx < num_metrics; idx++) {
        metrics.add(String.format("metric-%05d.yugabyte.com", idx));
      }
    }

    public String getUserId() {
      return user_id;
    }

    public String getNodeId() {
      return node_id;
    }

    public List<String> getMetrics() {
      return metrics;
    }

    public String getRandomMetricId() {
      return metrics.get(random.nextInt(metrics.size()));
    }

    public long getEndTs() {
      return getLastEmittedTs() + 1;
    }

    public long getStartTs() {
      // Return an interval that reads 30-120 data points.
      long deltaT = 30L * (1 + random.nextInt(90)) * dataEmitRateMs;
      return getEndTs() - deltaT;
    }

    @Override
    public String toString() {
      return getId() + ":" + "[" + getMetrics().size() + " metrics]";
    }
  }
}
