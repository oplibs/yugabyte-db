// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.typesafe.config.Config;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunHooks;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.common.utils.NaturalOrderComparator;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class HookInserter {

  private static String HOOK_ROOT_PATH = "/tmp";
  private static final String ENABLE_CUSTOM_HOOKS_PATH =
      "yb.security.custom_hooks.enable_custom_hooks";
  private static final String ENABLE_SUDO_PATH = "yb.security.custom_hooks.enable_sudo";

  public static void addHookTrigger(
      TriggerType trigger,
      AbstractTaskBase task,
      UniverseTaskParams universeParams,
      Collection<NodeDetails> nodes) {
    if (!task.config.getBoolean(ENABLE_CUSTOM_HOOKS_PATH)) return;
    List<Pair<Hook, Collection<NodeDetails>>> executionPlan =
        getExecutionPlan(trigger, universeParams, task.config, nodes);

    for (Pair<Hook, Collection<NodeDetails>> singleHookPlan : executionPlan) {
      Hook hook = singleHookPlan.getFirst();
      Collection<NodeDetails> targetNodes = singleHookPlan.getSecond();

      // Create the hook script to run
      SubTaskGroup subTaskGroup =
          task.createSubTaskGroup(
              "Hook-" + task.userTaskUUID + "-" + hook.name, SubTaskGroupType.RunningHooks);
      for (NodeDetails node : targetNodes) {
        RunHooks.Params taskParams = new RunHooks.Params();
        taskParams.creatingUser = universeParams.creatingUser;
        taskParams.hook = hook;
        taskParams.hookPath = HOOK_ROOT_PATH + "/" + node.nodeUuid + "-" + hook.name;
        taskParams.trigger = trigger;
        taskParams.nodeName = node.nodeName;
        taskParams.nodeUuid = node.nodeUuid;
        taskParams.azUuid = node.azUuid;
        taskParams.universeUUID = universeParams.universeUUID;
        taskParams.parentTask = task.getClass().getSimpleName();
        RunHooks runHooks = AbstractTaskBase.createTask(RunHooks.class);
        runHooks.initialize(taskParams);
        subTaskGroup.addSubTask(runHooks);
      }

      task.getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
  }

  // Get all the hooks and their targets, and then order them in natural order.
  private static List<Pair<Hook, Collection<NodeDetails>>> getExecutionPlan(
      TriggerType trigger,
      UniverseTaskParams universeParams,
      Config config,
      Collection<NodeDetails> nodes) {
    boolean isSudoEnabled = config.getBoolean(ENABLE_SUDO_PATH);
    List<Pair<Hook, Collection<NodeDetails>>> executionPlan =
        new ArrayList<Pair<Hook, Collection<NodeDetails>>>();
    UUID universeUUID = universeParams.universeUUID;
    Universe universe = Universe.getOrBadRequest(universeUUID);
    UUID customerUUID = Customer.get(universe.customerId).uuid;

    // Get global hooks
    HookScope globalScope = HookScope.getByTriggerScopeId(customerUUID, trigger, null, null);
    addHooksToExecutionPlan(executionPlan, globalScope, nodes, isSudoEnabled);

    // Get provider hooks
    // How:
    // 1. Bucket nodes by provider UUID
    // 2. Add the hooks to the excution plan
    Map<UUID, List<NodeDetails>> nodeProviderMap = new HashMap<>();
    for (NodeDetails node : nodes) {
      Cluster cluster = universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
      UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
      nodeProviderMap.computeIfAbsent(providerUUID, k -> new ArrayList<>()).add(node);
    }
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodeProviderMap.entrySet()) {
      UUID providerUUID = entry.getKey();
      List<NodeDetails> providerNodes = entry.getValue();
      HookScope providerScope =
          HookScope.getByTriggerScopeId(customerUUID, trigger, null, providerUUID);
      addHooksToExecutionPlan(executionPlan, providerScope, providerNodes, isSudoEnabled);
    }

    // Get universe hooks
    HookScope universeScope =
        HookScope.getByTriggerScopeId(customerUUID, trigger, universeUUID, null);
    addHooksToExecutionPlan(executionPlan, universeScope, nodes, isSudoEnabled);

    // Sort in natural order
    NaturalOrderComparator comparator = new NaturalOrderComparator();
    Collections.sort(
        executionPlan,
        (a, b) -> {
          return comparator.compare(a.getFirst().name, b.getFirst().name);
        });

    return executionPlan;
  }

  private static void addHooksToExecutionPlan(
      List<Pair<Hook, Collection<NodeDetails>>> executionPlan,
      HookScope hookScope,
      Collection<NodeDetails> nodes,
      boolean isSudoEnabled) {
    if (hookScope == null) return;
    for (Hook hook : hookScope.getHooks()) {
      if (!isSudoEnabled && hook.useSudo) {
        log.debug("Sudo execution is not enabled, ignoring {}", hook.name);
        continue;
      }
      executionPlan.add(new Pair<Hook, Collection<NodeDetails>>(hook, nodes));
    }
  }
}
