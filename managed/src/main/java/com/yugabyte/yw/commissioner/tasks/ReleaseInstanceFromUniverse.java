// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import java.util.Arrays;
import java.util.HashSet;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

// Allows the removal of the instance from a universe. That node is already not part of the
// universe and is in Removed state.
public class ReleaseInstanceFromUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ReleaseInstanceFromUniverse.class);

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams)taskParams;
  }

  @Override
  public void run() {
    LOG.info("Started {} task for node {} in univ uuid={}", getName(),
             taskParams().nodeName, taskParams().universeUUID);

    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      if (currentNode.state != NodeDetails.NodeState.Removed &&
          currentNode.state != NodeDetails.NodeState.Stopped) {
        String msg = "Node " + taskParams().nodeName + " is not on removed or added state, but " +
                     "is in " + currentNode.state + ", so cannot be released.";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      // Update Node State to BeingDecommissioned.
      createSetNodeStateTask(currentNode, NodeState.BeingDecommissioned)
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      if (instanceExists(taskParams())) {
        // Create a task for removal from blacklist of this server.
        createModifyBlackListTask(Arrays.asList(currentNode), false /* isAdd */)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
      
        // Create tasks to terminate that instance.
        createDestroyServerTasks(new HashSet<NodeDetails>(Arrays.asList(currentNode)), false, false)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
      }

      // Update Node State to Decommissioned.
      createSetNodeStateTask(currentNode, NodeState.Decommissioned)
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent.providerType,
                                userIntent.provider, userIntent.universeName)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

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
}
