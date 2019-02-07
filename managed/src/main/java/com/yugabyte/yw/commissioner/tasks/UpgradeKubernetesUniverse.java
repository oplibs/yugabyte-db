// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.models.Universe;

import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;

public class UpgradeKubernetesUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpgradeKubernetesUniverse.class);

  public static class Params extends RollingRestartParams {}

  @Override
  protected RollingRestartParams taskParams() {
    return (RollingRestartParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      if (taskParams().taskType == UpgradeUniverse.UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null ||
            taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
      }

      switch (taskParams().taskType) {
        case Software:
          LOG.info("Upgrading software version to {} in universe {}",
                   taskParams().ybSoftwareVersion, universe.name);

          createUpgradeTask(userIntent);

          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
          break;
        case GFlags:
          LOG.info("Upgrading GFlags in universe {}", universe.name);
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpgradeTask(userIntent);
          break;
      }

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

    private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      default:
        return SubTaskGroupType.Invalid;
    }
  }

  private void createUpgradeTask(UserIntent userIntent) {
    String version = null;
    boolean flag = true;
    if (taskParams().taskType == UpgradeUniverse.UpgradeTaskType.Software) {
       version = taskParams().ybSoftwareVersion;
       flag = false;
    }
    if (!taskParams().masterGFlags.isEmpty() || !flag) {
      for (int partition = userIntent.replicationFactor - 1; partition >= 0; partition--) {
        createKubernetesExecutorTaskForServerType(KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
            version, ServerType.MASTER, partition);
        createKubernetesWaitForPodTask(KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
            String.format("yb-master-%d", partition), taskParams().sleepAfterMasterRestartMillis / 1000);
      }
    }
    if (!taskParams().tserverGFlags.isEmpty() || !flag) {
      userIntent.tserverGFlags = taskParams().tserverGFlags;
      for (int partition = userIntent.numNodes - 1; partition >= 0; partition--) {
        createKubernetesExecutorTaskForServerType(KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
            version, ServerType.TSERVER, partition);
        createKubernetesWaitForPodTask(KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
            String.format("yb-tserver-%d", partition), taskParams().sleepAfterTServerRestartMillis / 1000);
      }
    }
    createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO);
  }
}
