// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Set;
import java.util.UUID;

public class CreateKubernetesUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateKubernetesUniverse.class);

  @Override
  public void run() {
    try {
      // Verify the task params.
      verifyParams();
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      if (PlacementInfoUtil.getNumMasters(taskParams().nodeDetailsSet) > 0) {
        throw new IllegalStateException("Should not have any masters before create task run.");
      }
      UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams().getPrimaryCluster();
      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
      PlacementInfoUtil.selectMasters(primaryNodes, primaryCluster.userIntent.replicationFactor);

      // Update the user intent.
      writeUserIntentToUniverse();

      // Set the correct node names as they are finalized now. This is done just in case the user
      // changes the universe name before submitting.
      updateNodeNames();

      // In case of Kubernetes create we would do Helm Init with Service account, then do
      // Helm install the YugaByte helm chart and fetch the pod info for the IP addresses.
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.HELM_INIT);
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO);

      createSwamperTargetUpdateTask(false);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  private void createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType commandType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    UniverseDefinitionTaskParams.Cluster primary = taskParams().getPrimaryCluster();
    params.providerUUID = UUID.fromString(
        primary.userIntent.provider);
    params.commandType = commandType;
    params.nodePrefix = taskParams().nodePrefix;
    params.universeUUID = taskParams().universeUUID;
    KubernetesCommandExecutor task = new KubernetesCommandExecutor();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }
}
