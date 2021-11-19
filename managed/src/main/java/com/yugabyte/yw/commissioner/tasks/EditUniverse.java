/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
@Slf4j
public class EditUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected EditUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().universeUUID);
    String errorString = null;

    try {
      checkUniverseVersion();
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the changes to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      preTaskActions();

      // Set all the node names.
      setNodeNames(universe);

      updateOnPremNodeUuidsOnTaskParams();

      // Run preflight checks on onprem nodes to be added.
      if (performUniversePreflightChecks(taskParams().clusters)) {
        // Select master nodes, if needed. Changes in masters are not automatically
        // applied.
        SelectMastersResult selection = selectMasters(universe.getMasterLeaderHostText());
        verifyMastersSelection(selection);

        // Applying changes to master flags for added masters only.
        // We are not clearing this flag according to selection.removedMasters in case
        // the master leader is to be changed and until the master leader is switched to
        // the new one.
        selection.addedMasters.forEach(n -> n.isMaster = true);

        // Update the user intent.
        writeUserIntentToUniverse(false);

        for (Cluster cluster : taskParams().clusters) {
          addDefaultGFlags(cluster.userIntent);
          editCluster(
              universe,
              cluster,
              getNodesInCluster(cluster.uuid, selection.addedMasters),
              getNodesInCluster(cluster.uuid, selection.removedMasters));
        }

        // Wait for the master leader to hear from all tservers.
        createWaitForTServerHeartBeatsTask()
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Update the DNS entry for this universe, based in primary provider info.
        UserIntent primaryIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
        createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, primaryIntent)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Marks the update of this universe as a success only if all the tasks before it succeeded.
        createMarkUniverseUpdateSuccessTasks()
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      } else {
        errorString = "Preflight checks failed.";
      }

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      errorString = t.getMessage();
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate(errorString);
    }
    log.info("Finished {} task.", getName());
  }

  private void editCluster(
      Universe universe,
      Cluster cluster,
      Set<NodeDetails> newMasters,
      Set<NodeDetails> mastersToStop) {
    UserIntent userIntent = cluster.userIntent;
    Set<NodeDetails> nodes = taskParams().getNodesInCluster(cluster.uuid);

    log.info(
        "Configure numNodes={}, Replication factor={}",
        userIntent.numNodes,
        userIntent.replicationFactor);

    Collection<NodeDetails> nodesToBeRemoved = PlacementInfoUtil.getNodesToBeRemoved(nodes);

    Collection<NodeDetails> nodesToProvision = PlacementInfoUtil.getNodesToProvision(nodes);

    List<NodeDetails> existingNodesToStartMaster =
        newMasters
            .stream()
            .filter(n -> n.state != NodeState.ToBeAdded)
            .collect(Collectors.toList());

    // Set the old nodes' state to to-be-removed.
    if (!nodesToBeRemoved.isEmpty()) {
      createSetNodeStateTasks(nodesToBeRemoved, NodeDetails.NodeState.ToBeRemoved)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    }

    Set<NodeDetails> liveNodes = PlacementInfoUtil.getLiveNodes(nodes);

    // Update any tags on nodes that are not going to be removed and not being added.
    Cluster existingCluster = universe.getCluster(cluster.uuid);
    if (!cluster.areTagsSame(existingCluster)) {
      log.info(
          "Tags changed from '{}' to '{}'.",
          existingCluster.userIntent.instanceTags,
          cluster.userIntent.instanceTags);
      createUpdateInstanceTagsTasks(
          getNodesInCluster(cluster.uuid, liveNodes),
          Util.getKeysNotPresent(
              existingCluster.userIntent.instanceTags, cluster.userIntent.instanceTags));
    }

    if (!nodesToProvision.isEmpty()) {
      Map<UUID, List<NodeDetails>> nodesPerAZ =
          nodes
              .stream()
              .filter(
                  n ->
                      n.state != NodeDetails.NodeState.ToBeAdded
                          && n.state != NodeDetails.NodeState.ToBeRemoved)
              .collect(Collectors.groupingBy(n -> n.azUuid));

      nodesToProvision.forEach(
          node -> {
            Set<String> machineImages =
                nodesPerAZ
                    .getOrDefault(node.azUuid, Collections.emptyList())
                    .stream()
                    .map(n -> n.machineImage)
                    .collect(Collectors.toSet());
            Iterator<String> iterator = machineImages.iterator();

            if (iterator.hasNext()) {
              String imageToUse = iterator.next();

              if (iterator.hasNext()) {
                log.warn(
                    "Nodes in AZ {} are based on different machine images: {},"
                        + " falling back to default",
                    node.cloudInfo.az,
                    String.join(", ", machineImages));
              } else {
                node.machineImage = imageToUse;
              }
            }
          });

      // Create the required number of nodes in the appropriate locations.
      createCreateServerTasks(nodesToProvision).setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(nodesToProvision).setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Provision the required nodes so that Yugabyte software can be deployed.
      createSetupServerTasks(nodesToProvision).setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(nodesToProvision, true /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Override master (on primary cluster only) and tserver flags as necessary.
      if (cluster.clusterType == ClusterType.PRIMARY) {
        createGFlagsOverrideTasks(nodesToProvision, ServerType.MASTER);
      }
      createGFlagsOverrideTasks(nodesToProvision, ServerType.TSERVER);
    }

    Set<NodeDetails> removeMasters = PlacementInfoUtil.getMastersToBeRemoved(nodes);
    Set<NodeDetails> tserversToBeRemoved = PlacementInfoUtil.getTserversToBeRemoved(nodes);

    // Ensure all masters are covered in nodes to be removed.
    if (!removeMasters.isEmpty()) {
      if (nodesToBeRemoved.isEmpty()) {
        String errMsg = "If masters are being removed, corresponding nodes need removal too.";
        log.error(errMsg + " masters: " + nodeNames(removeMasters));
        throw new IllegalStateException(errMsg);
      }
      if (!nodesToBeRemoved.containsAll(removeMasters)) {
        String errMsg = "If masters are being removed, all those nodes need removal too.";
        log.error(
            errMsg
                + " masters: "
                + nodeNames(removeMasters)
                + " , but removing only "
                + nodeNames(nodesToBeRemoved));
        throw new IllegalStateException(errMsg);
      }
    }
    removeMasters.addAll(mastersToStop);

    // All necessary nodes are created. Data moving will coming soon.
    createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.ToJoinCluster)
        .setSubTaskGroupType(SubTaskGroupType.Provisioning);

    // Creates the primary cluster by first starting the masters.
    if (!newMasters.isEmpty()) {
      if (cluster.clusterType == ClusterType.ASYNC) {
        String errMsg = "Read-only cluster " + cluster.uuid + " should not have masters.";
        log.error(errMsg);
        throw new IllegalStateException(errMsg);
      }

      // Update master configuration on the nodes which are already running.
      if (!existingNodesToStartMaster.isEmpty()) {
        createConfigureServerTasks(
                existingNodesToStartMaster,
                true /* isShell */,
                true /* updateMasterAddrs */,
                true /* isMaster */)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      createStartMasterTasks(newMasters).setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for masters to be responsive.
      createWaitForServersTasks(newMasters, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    Set<NodeDetails> newTservers = PlacementInfoUtil.getTserversToProvision(nodes);
    if (!newTservers.isEmpty()) {
      // Blacklist all the new tservers before starting so that they do not join
      createModifyBlackListTask(newTservers, null /* To remove */, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Start the tservers in the clusters.
      createStartTServersTasks(newTservers).setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for all tablet servers to be responsive.
      createWaitForServersTasks(newTservers, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    if (!nodesToProvision.isEmpty()) {
      // Set the new nodes' state to live.
      createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    if (!tserversToBeRemoved.isEmpty()) {
      // Swap the blacklisted tservers
      createModifyBlackListTask(tserversToBeRemoved, newTservers, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Update placement info on master leader.
    createPlacementInfoTask(null /* additional blacklist */)
        .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

    // Update the swamper target file.
    createSwamperTargetUpdateTask(false /* removeFile */);

    if (!nodesToBeRemoved.isEmpty()) {
      // Wait for %age completion of the tablet move from master.
      createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    } else {
      if (!tserversToBeRemoved.isEmpty()) {
        String errMsg = "Universe shrink should have been handled using node decommision.";
        log.error(errMsg);
        throw new IllegalStateException(errMsg);
      }
      // If only tservers are added, wait for load to balance across all tservers.
      createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }

    if (cluster.clusterType == ClusterType.PRIMARY
        && PlacementInfoUtil.didAffinitizedLeadersChange(
            universe.getUniverseDetails().getPrimaryCluster().placementInfo,
            cluster.placementInfo)) {
      createWaitForLeadersOnPreferredOnlyTask();
    }

    if (!newMasters.isEmpty()) {
      // Now finalize the master quorum change tasks.
      createMoveMastersTasks(SubTaskGroupType.WaitForDataMigration, newMasters, removeMasters);

      if (!mastersToStop.isEmpty()) {
        createStopMasterTasks(mastersToStop)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Wait for a master leader to be elected.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update these older ones to be not masters anymore so tserver info can be updated with the
      // final master list and other future cluster client operations.
      createUpdateNodeProcessTasks(removeMasters, ServerType.MASTER, false);

      // Change the master addresses in the conf file for all tservers.
      Set<NodeDetails> allTservers = new HashSet<>(newTservers);
      allTservers.addAll(liveNodes);

      createConfigureServerTasks(allTservers, false /* isShell */, true /* updateMasterAddrs */);
      createSetFlagInMemoryTasks(
              allTservers,
              ServerType.TSERVER,
              true /* force flag update */,
              null /* no gflag to update */,
              true /* updateMasterAddrs */)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);

      Set<NodeDetails> allMasters = new HashSet<>(newMasters);
      Set<String> takenMasters =
          allMasters.stream().map(n -> n.nodeName).collect(Collectors.toSet());
      allMasters.addAll(
          liveNodes
              .stream()
              .filter(
                  n ->
                      n.isMaster
                          && !takenMasters.contains(n.nodeName)
                          && !mastersToStop.contains(n))
              .collect(Collectors.toSet()));

      // Change the master addresses in the conf file for the new masters.
      createConfigureServerTasks(
          allMasters, false /* isShell */, true /* updateMasterAddrs */, true /* isMaster */);
      createSetFlagInMemoryTasks(
              allMasters,
              ServerType.MASTER,
              true /* force flag update */,
              null /* no gflag to update */,
              true /* updateMasterAddrs */)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    }

    // Finally send destroy to the old set of nodes and remove them from this universe.
    if (!nodesToBeRemoved.isEmpty()) {
      createDestroyServerTasks(nodesToBeRemoved, false /* isForceDelete */, true /* deleteNode */)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    }
  }

  /**
   * Fills in the series of steps needed to move the masters using the node names. The actual node
   * details (such as ip addresses) are found at runtime by querying the database.
   */
  private void createMoveMastersTasks(
      SubTaskGroupType subTask, Set<NodeDetails> newMasters, Set<NodeDetails> removeMasters) {

    // Get the list of node names to add as masters.
    List<NodeDetails> mastersToAdd = new ArrayList<>(newMasters);
    // Get the list of node names to remove as masters.
    List<NodeDetails> mastersToRemove = new ArrayList<>(removeMasters);

    // Find the minimum number of master changes where we can perform an add followed by a remove.
    int numIters = Math.min(mastersToAdd.size(), mastersToRemove.size());

    // Perform a master add followed by a remove if possible. Need not remove the (current) master
    // leader last - even if we get current leader, it might change by the time we run the actual
    // task. So we might do multiple leader stepdown's, which happens automatically in the
    // client code during the task's run.
    for (int idx = 0; idx < numIters; idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
      // Do not use useHostPort = true because retry is not done for the option
      // when the leader itself is being removed. The retryable error code
      // LEADER_NEEDS_STEP_DOWN is reported only when useHostPort = false.
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask, false);
    }

    // Perform any additions still left.
    for (int idx = numIters; idx < newMasters.size(); idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
    }

    // Perform any removals still left.
    for (int idx = numIters; idx < removeMasters.size(); idx++) {
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask, false);
    }
  }

  private static Set<NodeDetails> getNodesInCluster(UUID uuid, Set<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.isInPlacement(uuid)).collect(Collectors.toSet());
  }
}
