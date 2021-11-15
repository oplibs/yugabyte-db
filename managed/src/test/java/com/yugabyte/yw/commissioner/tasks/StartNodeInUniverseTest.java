// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class StartNodeInUniverseTest extends CommissionerBaseTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Universe defaultUniverse;

  private Region region;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    YBClient mockClient = mock(YBClient.class);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    try {
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    } catch (Exception e) {
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(
        defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

    Map<String, String> gflags = new HashMap<>();
    gflags.put("foo", "bar");
    defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags = gflags;

    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
    return submitTask(taskParams, nodeName, 2);
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName, int expectedVersion) {
    taskParams.clusters.addAll(
        Universe.getOrBadRequest(taskParams.universeUUID).getUniverseDetails().clusters);
    taskParams.expectedUniverseVersion = expectedVersion;
    taskParams.nodeName = nodeName;
    try {
      UUID taskUUID = commissioner.submit(TaskType.StartNodeInUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<TaskType> START_NODE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> START_NODE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of("state", "Starting")),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> WITH_MASTER_UNDER_REPLICATED =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          TaskType.ChangeMasterConfig,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          // The following four tasks comes from "MasterInfoUpdateTask" and must be done
          // after tserver is added
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> WITH_MASTER_UNDER_REPLICATED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of("state", "Starting")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertStartNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, boolean masterStartExpected) {
    int position = 0;
    if (masterStartExpected) {
      for (TaskType taskType : WITH_MASTER_UNDER_REPLICATED) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        assertEquals("At position: " + position, taskType, tasks.get(0).getTaskType());
        JsonNode expectedResults = WITH_MASTER_UNDER_REPLICATED_RESULTS.get(position);
        List<JsonNode> taskDetails =
            tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
        assertJsonEqual(expectedResults, taskDetails.get(0));
        position++;
      }
    } else {
      for (TaskType taskType : START_NODE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        assertEquals(1, tasks.size());
        assertEquals("At position: " + position, taskType, tasks.get(0).getTaskType());
        JsonNode expectedResults = START_NODE_TASK_EXPECTED_RESULTS.get(position);
        List<JsonNode> taskDetails =
            tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
        assertJsonEqual(expectedResults, taskDetails.get(0));
        position++;
      }
    }
  }

  @Test
  public void testAddNodeSuccess() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(2)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(START_NODE_TASK_SEQUENCE.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, false);
  }

  @Test
  public void testStartNodeWithUnderReplicatedMaster_WithoutReadOnlyCluster_NodeFromPrimary() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.universeUUID, ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(WITH_MASTER_UNDER_REPLICATED.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, true);
  }

  @Test
  public void testStartUnknownNode() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams, "host-n9");
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testStartNodeWithUnderReplicatedMaster_WithReadOnlyCluster_NodeFromPrimary() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.universeUUID,
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(false, 3));

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(12)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(WITH_MASTER_UNDER_REPLICATED.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, true /* Master start is expected */);
  }

  @Test
  public void testStartNodeWithUnderReplicatedMaster_WithReadOnlyCluster_NodeFromReadReplica() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.universeUUID,
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor = 5;
            });
    universe =
        Universe.saveDetails(
            universe.universeUUID,
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(true, 3));
    universe =
        Universe.saveDetails(
            universe.universeUUID,
            univ -> {
              // First node should be master, so stopping it should give
              // areMastersUnderReplicated = true.
              univ.getNode("host-n1").isMaster = false;
            });

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams, "yb-tserver-0", 4);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(2)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(START_NODE_TASK_SEQUENCE.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, false /* Master start is unexpected */);
  }

  @Test
  // @formatter:off
  @Parameters({"false, region-1, true", "true, region-1, true", "true, test-region, false"})
  // @formatter:on
  public void testStartNodeWithUnderReplicatedMaster_WithDefaultRegion(
      boolean isDefaultRegion, String regionCodeForFirstNode, boolean isMasterStart) {
    // 'test-region' for automatically created nodes.
    Region testRegion = Region.create(defaultProvider, "test-region", "Region 2", "yb-image-1");

    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.universeUUID,
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor = 5;
            });
    universe =
        Universe.saveDetails(
            universe.universeUUID, ApiUtils.mockUniverseUpdaterWithInactiveNodes(true));

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = universe.universeUUID;

    universe =
        Universe.saveDetails(
            universe.universeUUID,
            univ -> {
              // First node should be master, so stopping it should give
              // areMastersUnderReplicated = true.
              NodeDetails node = univ.getNode("host-n1");
              node.cloudInfo.region = regionCodeForFirstNode;
              node.state = NodeState.Stopped;
              node.isMaster = false;

              Cluster cluster = univ.getUniverseDetails().clusters.get(0);
              cluster.userIntent.regionList = ImmutableList.of(region.uuid, testRegion.uuid);
              cluster.placementInfo =
                  PlacementInfoUtil.getPlacementInfo(
                      ClusterType.PRIMARY,
                      cluster.userIntent,
                      cluster.userIntent.replicationFactor,
                      region.uuid);
              if (isDefaultRegion) {
                cluster.placementInfo.cloudList.get(0).defaultRegion = region.uuid;
              }
            });

    TaskInfo taskInfo = submitTask(taskParams, "host-n1", 4);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(isMasterStart ? 15 : 2)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(
        isMasterStart ? WITH_MASTER_UNDER_REPLICATED.size() : START_NODE_TASK_SEQUENCE.size(),
        subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, isMasterStart);
  }
}
