// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForTServerHeartBeats;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;

/**
 * Abstract base class for all tasks that create/edit the universe definition. These include the
 * create universe task and all forms of edit universe tasks. Note that the delete universe task
 * extends the UniverseTaskBase, as it does not depend on the universe definition.
 */
public abstract class UniverseDefinitionTaskBase extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseDefinitionTaskBase.class);

  // Enum for specifying the server type.
  public enum ServerType {
    MASTER,
    TSERVER,
    YQLSERVER,
    REDISSERVER,
    EITHER
  }

  // The task params.
  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * Writes the user intent to the universe.
   */
  public Universe writeUserIntentToUniverse() {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        // Persist the updated information about the universe.
        // It should have been marked as being edited in lockUniverseForUpdate().
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (!universeDetails.updateInProgress) {
          String msg = "Universe " + taskParams().universeUUID +
                       " has not been marked as being updated.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }
        universeDetails.nodeDetailsSet = taskParams().nodeDetailsSet;
        universeDetails.nodePrefix = taskParams().nodePrefix;
        universeDetails.universeUUID = taskParams().universeUUID;
        Cluster cluster = taskParams().getPrimaryCluster();
        universeDetails.upsertPrimaryCluster(cluster.userIntent, cluster.placementInfo);
        universeDetails.clusters.removeIf(c -> c.clusterType.equals(ClusterType.ASYNC));
        universeDetails.clusters.addAll(taskParams().getReadOnlyClusters());
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Wrote user intent for universe {}.", taskParams().universeUUID);
    // Return the universe object that we have already updated.
    return universe;
  }

  // Helper data structure to save the new name and index of nodes for quick lookup using the
  // old name of nodes.
  private class NameAndIndex {
    String name;
    int index;

    public NameAndIndex(String name, int index) {
      this.name = name;
      this.index = index;
    }
    
    public String toString() {
      return "{name: " + name + ", index: " + index + "}";
    }
  }

  // Fix up the name of all the nodes. This fixes the name and the node index for being created
  // nodes. Since universe name can be changed in the UI, and configure is not called
  // before submitting Create, we fix up the node-prefix also to latest universe name.
  public void updateNodeNames() {
    Collection<NodeDetails> nodes = taskParams().nodeDetailsSet;
    Universe universe = Universe.get(taskParams().universeUUID);
    int iter = 0;
    int startIndex = PlacementInfoUtil.getStartIndex(universe.getNodes());
    final Map<String, NameAndIndex> oldToNewName = new HashMap<String, NameAndIndex>();
    String nodePrefix = taskParams().nodePrefix;
    // Pick the univese name from the current in-memory state.
    String univNewName = taskParams().getPrimaryCluster().userIntent.universeName;
    boolean updateNodePrefix = !nodePrefix.contains(univNewName);
    final boolean univNameChanged = !universe.name.equals(univNewName);

    // Note that `universe` should have the new name persisted before this call.
    if (updateNodePrefix) {
      if (univNameChanged) {
    	LOG.warn("Universe name mismatched: expected {} but found {}. Updating to {}.",
    			 univNewName, universe.name, univNewName);
      }
      nodePrefix = Util.getNodePrefix(universe.customerId, univNewName);
      LOG.info("Updating node prefix to {}.", nodePrefix);
    }

    for (NodeDetails node : nodes) {
      if (node.state == NodeDetails.NodeState.ToBeAdded) {
        node.nodeIdx = startIndex + iter;
        String newName = nodePrefix + "-n" + node.nodeIdx;
        LOG.info("Changing in-memory node name from {} to {}.", node.nodeName , newName);
        oldToNewName.put(node.nodeName, new NameAndIndex(newName, node.nodeIdx));
        node.nodeName = newName;
        iter++;
      }
    }

    PlacementInfoUtil.ensureUniqueNodeNames(taskParams().nodeDetailsSet);

    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        Collection<NodeDetails> univNodes = universe.getNodes();
        for (NodeDetails node : univNodes) {
          if (node.state == NodeDetails.NodeState.ToBeAdded) {
            // Since we have already set the 'updateInProgress' flag on this universe in the DB and
            // this step is single threaded, we are guaranteed no one else will be modifying it.
            NameAndIndex newInfo = oldToNewName.get(node.nodeName);
            LOG.info("Changing node name from {} to newInfo={}.", node.nodeName, newInfo);
            node.nodeName = newInfo.name;
            node.nodeIdx = newInfo.index;
          }
        }
        if (univNameChanged) {
          universe.name = univNewName;
        }
      }
    };
    universe = Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Updated {} nodes in universe {}.", taskParams().nodeDetailsSet.size(),
              taskParams().universeUUID);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    List<Cluster> onPremClusters = universeDetails.clusters.stream()
            .filter(c -> c.userIntent.providerType.equals(CloudType.onprem))
            .collect(Collectors.toList());
    for (Cluster onPremCluster : onPremClusters) {
      Map<UUID, List<String>> onpremAzToNodes = new HashMap<UUID, List<String>>();
      for (NodeDetails node : universeDetails.getNodesInCluster(onPremCluster.uuid)) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          List<String> nodeNames = onpremAzToNodes.getOrDefault(node.azUuid, new ArrayList<String>());
          nodeNames.add(node.nodeName);
          onpremAzToNodes.put(node.azUuid, nodeNames);
        }
      }
      // Update in-memory map.
      String instanceType = onPremCluster.userIntent.instanceType;
      Map<String, NodeInstance> nodeMap = NodeInstance.pickNodes(onpremAzToNodes, instanceType);
      for (NodeDetails node : taskParams().nodeDetailsSet) {
        // TODO: use the UUID to select the node, but this requires a refactor of the tasks/params
        // to more easily trickle down this uuid into all locations.
        NodeInstance n = nodeMap.get(node.nodeName);
        if (n != null) {
          node.nodeUuid = n.nodeUuid;
        }
      }
    }
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createSetupServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleSetupServer", executor);

    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region code.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Pick one of the subnets in a round robin fashion.
      params.subnetId = node.cloudInfo.subnet_id;
      // Set the instance type.
      params.instanceType = userIntent.instanceType;
      // Set the spot price.
      params.spotPrice = userIntent.spotPrice;
      // set the assign public ip param
      params.assignPublicIP = userIntent.assignPublicIP;
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = new AnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(ansibleSetupServer);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list for fetching information about the nodes provisioned (such as the ip
   * address) and adds it to the task queue. This is specific to the cloud.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createServerInfoTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleUpdateNodeInfo", executor);

    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      NodeTaskParams params = new NodeTaskParams();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = new AnsibleUpdateNodeInfo();
      ansibleFindCloudHost.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(ansibleFindCloudHost);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to configure the newly provisioned nodes and adds it to the task queue.
   * Includes tasks such as setting up the 'yugabyte' user and installing the passed in software
   * package.
   *
   * @param nodes : a collection of nodes that need to be created
   * @param isMasterInShellMode : true if we are configuring a master node in shell mode
   */
  public SubTaskGroup createConfigureServerTasks(Collection<NodeDetails> nodes,
                                                 boolean isMasterInShellMode) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers", executor);

    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Sets the isMaster field
      params.isMaster = node.isMaster;
      // Set if this node is a master in shell mode.
      params.isMasterInShellMode = isMasterInShellMode;
      // The software package to install for this cluster.
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.type = UpgradeUniverse.UpgradeTaskType.Everything;
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createGFlagsOverrideTasks(Collection<NodeDetails> nodes,
                                                ServerType taskType) {
    // Skip if no extra flags for MASTER in primary cluster.
    if (taskType.equals(ServerType.MASTER) &&
            taskParams().getPrimaryCluster().userIntent.masterGFlags.isEmpty()) {
      return null;
    }
    // Skip if all clusters have no extra TSERVER flags. (No cluster has an extra TSERVER flag.)
    if (taskType.equals(ServerType.TSERVER) &&
            taskParams().clusters.stream().allMatch(c -> c.userIntent.tserverGFlags.isEmpty())) {
      return null;
    }

    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServersGFlags", executor);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add task type
      params.type = UpgradeUniverse.UpgradeTaskType.GFlags;
      params.setProperty("processType", taskType.toString());
      params.gflags = taskType.equals(ServerType.MASTER)
        ? userIntent.masterGFlags
        : userIntent.tserverGFlags;
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the masters on the given nodes and adds it to the task queue.
   *
   * @param nodes   : a collection of nodes that need master process started.
   * @param isShell : Determines if the masters should be started in shell mode.
   */
  public SubTaskGroup createStartMasterTasks(Collection<NodeDetails> nodes,
                                             boolean isShell) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "master";
      params.command = isShell ? "start" : "create";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the tservers on the set of passed in nodes and adds it to the task
   * queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createStartTServersTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "tserver";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForMasterLeader", executor);
    WaitForMasterLeader task = new WaitForMasterLeader();
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to wait for a minimum number of tservers to heartbeat
   * to the master leader.
   */
  public SubTaskGroup createWaitForTServerHeartBeatsTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForTServerHeartBeats", executor);
    WaitForTServerHeartBeats task = new WaitForTServerHeartBeats();
    WaitForTServerHeartBeats.Params params = new WaitForTServerHeartBeats.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to update the placement information by making a call to the master leader
   * of the cluster just created and adds it to the task queue.
   *
   * @param blacklistNodes    list of nodes which are being decommissioned.
   */
  public SubTaskGroup createPlacementInfoTask(Collection<NodeDetails> blacklistNodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("UpdatePlacementInfo", executor);
    UserIntent userIntent = taskParams().getPrimaryCluster().userIntent;
    UpdatePlacementInfo.Params params = new UpdatePlacementInfo.Params();
    // Set the cloud name.
    params.cloud = userIntent.providerType;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Set the number of masters.
    params.numReplicas = userIntent.replicationFactor;
    // Set the blacklist nodes if any are passed in.
    if (blacklistNodes != null && !blacklistNodes.isEmpty()) {
      Set<String> blacklistNodeNames = new HashSet<String>();
      for (NodeDetails node : blacklistNodes) {
        blacklistNodeNames.add(node.nodeName);
      }
      params.blacklistNodes = blacklistNodeNames;
    }
    // Create the task to update placement info.
    UpdatePlacementInfo task = new UpdatePlacementInfo();
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Verify that the task params are valid.
   */
  public void verifyParams() {
    if (taskParams().universeUUID == null) {
      throw new RuntimeException(getName() + ": universeUUID not set");
    }
    if (taskParams().nodePrefix == null) {
      throw new RuntimeException(getName() + ": nodePrefix not set");
    }
    for (Cluster cluster : taskParams().clusters) {
      PlacementInfoUtil.verifyNodesAndRF(cluster.userIntent.numNodes,
              cluster.userIntent.replicationFactor);
    }
  }
}
