// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.Set;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.Common;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.UniverseOpType;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class CreateUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateUniverse.class);

  @Override
  public void run() {
    LOG.info("Started {} task.", getName());
    try {
      // Verify the task params.
      verifyParams(UniverseOpType.CREATE);

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Set all the in-memory node names.
      setNodeNames(UniverseOpType.CREATE, universe);

      // Select master nodes.
      selectMasters();

      // Update the user intent.
      writeUserIntentToUniverse();

      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(taskParams().nodeDetailsSet, false /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      Cluster primaryCluster = taskParams().getPrimaryCluster();
      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
      // Override master flags (on primary cluster) and tserver flags as necessary.
      createGFlagsOverrideTasks(primaryNodes, ServerType.MASTER);
      createGFlagsOverrideTasks(taskParams().nodeDetailsSet, ServerType.TSERVER);

      // Get the new masters from the node list.
      Set<NodeDetails> newMasters = PlacementInfoUtil.getMastersToProvision(primaryNodes);

      // Creates the YB cluster by starting the masters in the create mode.
      createStartMasterTasks(newMasters)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for new masters to be responsive.
      createWaitForServersTasks(newMasters, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Start the tservers in the clusters.
      createStartTServersTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(taskParams().nodeDetailsSet, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set the node state to live.
      createSetNodeStateTasks(taskParams().nodeDetailsSet, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a Master Leader to be elected.
      createWaitForMasterLeaderTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a master leader to hear from all the tservers.
      createWaitForTServerHeartBeatsTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Initialize YSQL database if enabled by the user.
      if (taskParams().getPrimaryCluster().userIntent.enableYSQL) {
        createInitYSQLTasks(taskParams().getTServers());
      }

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Create a simple redis table.
      createTableTask(Common.TableType.REDIS_TABLE_TYPE, YBClient.REDIS_DEFAULT_TABLE_NAME, null)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the DNS entry for all the nodes once, using the primary cluster type.
      createDnsManipulationTask(DnsManager.DnsCommandType.Create, false,
                                primaryCluster.userIntent.providerType,
                                primaryCluster.userIntent.provider,
                                primaryCluster.userIntent.universeName)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
