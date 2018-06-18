// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
public class EditUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(EditUniverse.class);

  // Get the new masters from the node list.
  Set<NodeDetails> newMasters = new HashSet<NodeDetails>();

  @Override
  public void run() {
    LOG.info("Started {} task for uuid={}", getName(), taskParams().universeUUID);

    try {
      // Verify the task params.
      verifyParams();

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the changes to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Update the user intent.
      writeUserIntentToUniverse();

      // Set the correct node names as they are finalized now. This is done just in case the user
      // changes the universe name before submitting.
      updateNodeNames();

      for (Cluster cluster : taskParams().clusters) {
        editCluster(universe, cluster);
      }

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  private void editCluster(Universe universe, Cluster cluster) {
    UserIntent userIntent = cluster.userIntent;
    Set<NodeDetails> nodes = taskParams().getNodesInCluster(cluster.uuid);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    LOG.info("Configure numNodes={}, Replication factor={}", userIntent.numNodes,
             userIntent.replicationFactor);

    Collection<NodeDetails> nodesToBeRemoved =
        PlacementInfoUtil.getNodesToBeRemoved(nodes);

    Collection<NodeDetails> nodesToProvision =
        PlacementInfoUtil.getNodesToProvision(nodes);

    // Set the old nodes' state to to-be-removed.
    if (!nodesToBeRemoved.isEmpty()) {
      createSetNodeStateTasks(nodesToBeRemoved, NodeDetails.NodeState.ToBeRemoved)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    }

    if (!nodesToProvision.isEmpty()) {
      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(nodesToProvision, userIntent.deviceInfo)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(nodesToProvision, userIntent.deviceInfo)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(nodesToProvision, true /* isShell */,
                                 userIntent.deviceInfo, userIntent.ybSoftwareVersion)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Override master and tserver flags as necessary.
      createGFlagsOverrideTasks(nodesToProvision, ServerType.MASTER);
      createGFlagsOverrideTasks(nodesToProvision, ServerType.TSERVER);
    }

    newMasters = PlacementInfoUtil.getMastersToProvision(nodes);

    // Creates the primary cluster by first starting the masters.
    if (!newMasters.isEmpty()) {
      if (cluster.clusterType == ClusterType.ASYNC) {
        String errMsg = "Read-only cluster " + cluster.uuid + " should not have masters.";
        LOG.error(errMsg);
        throw new IllegalStateException(errMsg);
      }

      createStartMasterTasks(newMasters)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for masters to be responsive.
      createWaitForServersTasks(newMasters, ServerType.MASTER)
         .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    Set<NodeDetails> newTservers = PlacementInfoUtil.getTserversToProvision(nodes);

    if (!newTservers.isEmpty()) {
      // Start the tservers in the clusters.
      createStartTServersTasks(newTservers)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for all tablet servers to be responsive.
      createWaitForServersTasks(newTservers, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Remove them from blacklist, in case master is still tracking these.
      createModifyBlackListTask(new ArrayList(newTservers), false /* isAdd */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    if (!nodesToProvision.isEmpty()) {
      // Set the new nodes' state to live.
      createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    Collection<NodeDetails> tserversToBeRemoved = PlacementInfoUtil.getTserversToBeRemoved(nodes);

    // Persist the placement info and blacklisted node info into the YB master.
    // This is done after master config change jobs, so that the new master leader can perform
    // the auto load-balancing, and all tablet servers are heart beating to new set of masters.
    if (!nodesToBeRemoved.isEmpty()) {
      // Add any nodes to be removed to tserver removal to be considered for blacklisting.
      tserversToBeRemoved.addAll(nodesToBeRemoved);
    }

    // Update the blacklist servers on master leader.
    createPlacementInfoTask(tserversToBeRemoved)
        .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

    if (!nodesToBeRemoved.isEmpty()) {
      // Wait for %age completion of the tablet move from master.
      createWaitForDataMoveTask()
          .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    } else {
      if (!tserversToBeRemoved.isEmpty()) {
        String errMsg = "Universe shrink should have been handled using node decommision.";
        LOG.error(errMsg);
        throw new IllegalStateException(errMsg);
      }
      // If only tservers are added, wait for load to balance across all tservers.
      createWaitForLoadBalanceTask()
          .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }

    if (cluster.clusterType == ClusterType.PRIMARY &&
        PlacementInfoUtil.didAffinitizedLeadersChange(
            universeDetails.getPrimaryCluster().placementInfo,
            cluster.placementInfo)) {
      createWaitForLeadersOnPreferredOnlyTask();
    }

    if (!newMasters.isEmpty()) {
      // Now finalize the master quorum change tasks.
      createMoveMastersTasks(SubTaskGroupType.WaitForDataMigration);

      // Wait for a master leader to be elected.
      createWaitForMasterLeaderTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for the master leader to hear from all tservers.
      createWaitForTServerHeartBeatsTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Finally send destroy to the old set of nodes and remove them from this universe.
    if (!nodesToBeRemoved.isEmpty()) {
      createDestroyServerTasks(nodesToBeRemoved, false, true)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    }

    // Update the DNS entry for this universe.
    createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent.providerType,
                              userIntent.provider, userIntent.universeName)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /**
   * Fills in the series of steps needed to move the masters using the tag names of the nodes. The
   * actual node details (such as their ip addresses) are found at runtime by querying the database.
   */
  private void createMoveMastersTasks(SubTaskGroupType subTask) {
    // Get the list of node names to add as masters.
    List<NodeDetails> mastersToAdd = new ArrayList<>();
    for (NodeDetails node : newMasters) {
      mastersToAdd.add(node);
    }

    Collection<NodeDetails> removeMasters =
      PlacementInfoUtil.getMastersToBeRemoved(taskParams().nodeDetailsSet);

    // Get the list of node names to remove as masters.
    List<NodeDetails> mastersToRemove = new ArrayList<>();
    for (NodeDetails node : removeMasters) {
      mastersToRemove.add(node);
    }

    // Find the minimum number of master changes where we can perform an add followed by a remove.
    int numIters = Math.min(mastersToAdd.size(), mastersToRemove.size());

    // Perform a master add followed by a remove if possible. Need not remove the (current) master
    // leader last - even if we get current leader, it might change by the time we run the actual
    // task. So we might do multiple leader stepdown's, which happens automatically in the
    // client code during the task's run.
    for (int idx = 0; idx < numIters; idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask);
    }

    // Perform any additions still left.
    for (int idx = numIters; idx < newMasters.size(); idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
    }

    // Perform any removals still left.
    for (int idx = numIters; idx < removeMasters.size(); idx++) {
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask);
    }
  }
}
