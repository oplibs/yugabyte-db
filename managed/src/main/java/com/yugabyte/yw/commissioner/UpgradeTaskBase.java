// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageOtelCollector;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateClusterUserIntent;
import com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareUpgrade;
import com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareUpgradeYB;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public abstract class UpgradeTaskBase extends UniverseDefinitionTaskBase {

  private List<ServerType> canBeIgnoredServerTypes = Arrays.asList(ServerType.CONTROLLER);

  public static final UpgradeContext DEFAULT_CONTEXT =
      UpgradeContext.builder()
          .reconfigureMaster(false)
          .runBeforeStopping(false)
          .processInactiveMaster(false)
          .build();
  public static final UpgradeContext RUN_BEFORE_STOPPING =
      UpgradeContext.builder()
          .reconfigureMaster(false)
          .runBeforeStopping(true)
          .processInactiveMaster(false)
          .build();

  // Variable to mark if the loadbalancer state was changed.
  protected boolean isLoadBalancerOn = true;
  protected boolean hasRollingUpgrade = false;
  private MastersAndTservers nodesToBeRestarted;

  public static class MastersAndTservers {
    public final List<NodeDetails> mastersList;
    public final List<NodeDetails> tserversList;

    public MastersAndTservers(List<NodeDetails> mastersList, List<NodeDetails> tserversList) {
      this.mastersList = mastersList == null ? Collections.emptyList() : mastersList;
      this.tserversList = tserversList == null ? Collections.emptyList() : tserversList;
    }

    public Pair<List<NodeDetails>, List<NodeDetails>> asPair() {
      return Pair.of(new ArrayList<>(mastersList), new ArrayList<>(tserversList));
    }

    public MastersAndTservers getForCluster(UUID clusterUUID) {
      return new MastersAndTservers(
          mastersList.stream()
              .filter(n -> n.isInPlacement(clusterUUID))
              .collect(Collectors.toList()),
          tserversList.stream()
              .filter(n -> n.isInPlacement(clusterUUID))
              .collect(Collectors.toList()));
    }

    @Override
    public String toString() {
      return "{" + "mastersList=" + mastersList + ", tserversList=" + tserversList + '}';
    }

    public boolean isEmpty() {
      return CollectionUtils.isEmpty(mastersList) && CollectionUtils.isEmpty(tserversList);
    }

    public Set<NodeDetails> getAllNodes() {
      return Stream.concat(mastersList.stream(), tserversList.stream()).collect(Collectors.toSet());
    }
  }

  protected final MastersAndTservers getNodesToBeRestarted() {
    if (nodesToBeRestarted == null) {
      nodesToBeRestarted = calculateNodesToBeRestarted();
    }
    return nodesToBeRestarted;
  }

  protected abstract MastersAndTservers calculateNodesToBeRestarted();

  protected UpgradeTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    MastersAndTservers nodesToBeRestarted = getNodesToBeRestarted();
    log.debug("Nodes to be restarted {}", nodesToBeRestarted);
    if (taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE
        && nodesToBeRestarted != null
        && !nodesToBeRestarted.isEmpty()) {
      Optional<NodeDetails> nonLive =
          nodesToBeRestarted.getAllNodes().stream()
              .filter(n -> n.state != NodeState.Live)
              .findFirst();
      if (nonLive.isEmpty()) {
        createCheckNodesAreSafeToTakeDownTask(
            nodesToBeRestarted.mastersList,
            nodesToBeRestarted.tserversList,
            getTargetSoftwareVersion());
      }
    }
  }

  @Override
  protected void addBasicPrecheckTasks() {
    if (isFirstTry()) {
      verifyClustersConsistency();
    }
  }

  protected String getTargetSoftwareVersion() {
    return null;
  }

  @Override
  protected UpgradeTaskParams taskParams() {
    return (UpgradeTaskParams) taskParams;
  }

  @Deprecated
  // TODO This cannot serve all the stages in the upgrade because this gives only one fixed type.
  public abstract SubTaskGroupType getTaskSubGroupType();

  // State set on node while it is being upgraded
  public abstract NodeState getNodeState();

  public void runUpgrade(Runnable upgradeLambda) {
    checkUniverseVersion();
    lockAndFreezeUniverseForUpdate(taskParams().expectedUniverseVersion, null /* Txn callback */);
    try {
      Set<NodeDetails> nodeList = fetchAllNodes(taskParams().upgradeOption);

      // Run the pre-upgrade hooks
      createHookTriggerTasks(nodeList, true, false);

      // Execute the lambda which populates subTaskGroupQueue
      upgradeLambda.run();

      // Run the post-upgrade hooks
      createHookTriggerTasks(nodeList, false, false);

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error: ", getName(), t);

      if (taskParams().getUniverseSoftwareUpgradeStateOnFailure() != null) {
        Universe universe = getUniverse();
        if (!UniverseDefinitionTaskParams.IN_PROGRESS_UNIV_SOFTWARE_UPGRADE_STATES.contains(
            universe.getUniverseDetails().softwareUpgradeState)) {
          log.debug("Skipping universe upgrade state as actual task was not started.");
        } else {
          universe.updateUniverseSoftwareUpgradeState(
              taskParams().getUniverseSoftwareUpgradeStateOnFailure());
          log.debug(
              "Updated universe {} software upgrade state to  {}.",
              taskParams().getUniverseUUID(),
              taskParams().getUniverseSoftwareUpgradeStateOnFailure());
        }
      }

      // If the task failed, we don't want the loadbalancer to be
      // disabled, so we enable it again in case of errors.
      if (!isLoadBalancerOn) {
        setTaskQueueAndRun(
            () -> {
              createLoadBalancerStateChangeTask(true)
                  .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
            });
      }
      throw t;
    } finally {
      try {
        if (hasRollingUpgrade) {
          setTaskQueueAndRun(
              () -> clearLeaderBlacklistIfAvailable(SubTaskGroupType.ConfigureUniverse));
        }
      } finally {
        try {
          unlockXClusterUniverses(lockedXClusterUniversesUuidSet, false /* ignoreErrors */);
        } finally {
          unlockUniverseForUpdate();
        }
      }
    }
    log.info("Finished {} task.", getName());
  }

  public void createUpgradeTaskFlow(
      IUpgradeSubTask lambda,
      MastersAndTservers mastersAndTServers,
      UpgradeContext context,
      boolean isYbcPresent) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(lambda, mastersAndTServers, context, isYbcPresent);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(lambda, mastersAndTServers, context, isYbcPresent);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(lambda, mastersAndTServers, context);
        break;
    }
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      MastersAndTservers mastersAndTServers,
      UpgradeContext context,
      boolean isYbcPresent) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda,
        mastersAndTServers.mastersList,
        mastersAndTServers.tserversList,
        context,
        isYbcPresent);
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context,
      boolean isYbcPresent) {
    if (context.processInactiveMaster) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda,
          getInactiveMasters(masterNodes, tServerNodes),
          ServerType.MASTER,
          context,
          false,
          isYbcPresent);
    }

    if (context.processTServersFirst) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true, isYbcPresent);
    }

    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda, masterNodes, ServerType.MASTER, context, true, isYbcPresent);

    if (!context.processTServersFirst) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true, isYbcPresent);
    }
  }

  /**
   * Used for full node upgrades (for example resize) where all processes are stopped.
   *
   * @param lambda - for performing upgrade actions
   * @param nodeSet - set of nodes sorted in appropriate order.
   */
  public void createRollingNodesUpgradeTaskFlow(
      IUpgradeSubTask lambda,
      LinkedHashSet<NodeDetails> nodeSet,
      UpgradeContext context,
      boolean isYbcPresent) {
    createRollingUpgradeTaskFlow(
        lambda, nodeSet, NodeDetails::getAllProcesses, context, true, isYbcPresent);
  }

  private void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Collection<NodeDetails> nodes,
      ServerType baseProcessType,
      UpgradeContext context,
      boolean activeRole,
      boolean isYbcPresent) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda,
        nodes,
        node -> new HashSet<>(Arrays.asList(baseProcessType)),
        context,
        activeRole,
        isYbcPresent);
  }

  private void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Collection<NodeDetails> nodes,
      Function<NodeDetails, Set<ServerType>> processTypesFunction,
      UpgradeContext context,
      boolean activeRole,
      boolean isYbcPresent) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }
    hasRollingUpgrade = true;
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    Map<NodeDetails, Set<ServerType>> typesByNode = new HashMap<>();
    boolean hasTServer = false;
    for (NodeDetails node : nodes) {
      Set<ServerType> serverTypes = processTypesFunction.apply(node);
      hasTServer = hasTServer || serverTypes.contains(ServerType.TSERVER);
      typesByNode.put(node, serverTypes);
    }

    log.debug("types {} activeRole {}", typesByNode, activeRole);

    NodeState nodeState = getNodeState();

    if (hasTServer) {
      if (!isBlacklistLeaders()) {
        // Need load balancer on to perform leader blacklist.
        createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subGroupType);
        isLoadBalancerOn = false;
      } else {
        createModifyBlackListTask(
                Collections.emptyList() /* addNodes */,
                nodes /* removeNodes */,
                true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
      }
    }

    for (NodeDetails node : nodes) {
      Set<ServerType> processTypes = typesByNode.get(node);
      List<NodeDetails> singletonNodeList = Collections.singletonList(node);
      createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);

      createNodePrecheckTasks(
          node, processTypes, subGroupType, !activeRole, context.targetSoftwareVersion);

      // Run pre node upgrade hooks
      createHookTriggerTasks(singletonNodeList, true, true);
      if (context.runBeforeStopping) {
        rollingUpgradeLambda.run(singletonNodeList, processTypes);
      }

      if (isYbcPresent) {
        createServerControlTask(node, ServerType.CONTROLLER, "stop")
            .setSubTaskGroupType(subGroupType);
      }
      stopProcessesOnNode(
          node,
          processTypes,
          context.reconfigureMaster && activeRole /* remove master from quorum */,
          false /* deconfigure */,
          subGroupType);

      if (!context.runBeforeStopping) {
        rollingUpgradeLambda.run(singletonNodeList, processTypes);
      }

      if (activeRole) {
        for (ServerType processType : processTypes) {
          if (!context.skipStartingProcesses) {
            createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
          }
          createWaitForServersTasks(singletonNodeList, processType)
              .setSubTaskGroupType(subGroupType);
          if (processType.equals(ServerType.TSERVER) && node.isYsqlServer) {
            createWaitForServersTasks(
                    singletonNodeList,
                    ServerType.YSQLSERVER,
                    context.getUserIntent(),
                    context.getCommunicationPorts())
                .setSubTaskGroupType(subGroupType);
          }

          if (processType == ServerType.MASTER && context.reconfigureMaster) {
            // Add stopped master to the quorum.
            createChangeConfigTasks(node, true /* isAdd */, subGroupType);
          }
          createWaitForServerReady(node, processType).setSubTaskGroupType(subGroupType);
          // If there are no universe keys on the universe, it will have no effect.
          if (processType == ServerType.MASTER
              && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
            createSetActiveUniverseKeysTask().setSubTaskGroupType(subGroupType);
          }
        }
        createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);
        // remove leader blacklist
        if (processTypes.contains(ServerType.TSERVER)) {
          removeFromLeaderBlackListIfAvailable(Collections.singletonList(node), subGroupType);
        }
        if (isFollowerLagCheckEnabled()) {
          for (ServerType processType : processTypes) {
            createCheckFollowerLagTask(node, processType).setSubTaskGroupType(subGroupType);
          }
        }
        if (isYbcPresent) {
          if (!context.skipStartingProcesses) {
            createServerControlTask(node, ServerType.CONTROLLER, "start")
                .setSubTaskGroupType(subGroupType);
          }
          createWaitForYbcServerTask(new HashSet<>(singletonNodeList))
              .setSubTaskGroupType(subGroupType);
        }
      }

      if (context.postAction != null) {
        context.postAction.accept(node);
      }
      // Run post node upgrade hooks
      createHookTriggerTasks(singletonNodeList, false, true);
      createSetNodeStateTask(node, NodeState.Live).setSubTaskGroupType(subGroupType);

      createSleepAfterStartupTask(
          taskParams().getUniverseUUID(),
          processTypes,
          SetNodeState.getStartKey(node.getNodeName(), nodeState));
    }

    if (!isLoadBalancerOn) {
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(subGroupType);
      isLoadBalancerOn = true;
    }
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      MastersAndTservers mastersAndTServers,
      UpgradeContext context,
      boolean isYbcPresent) {
    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda,
        mastersAndTServers.mastersList,
        mastersAndTServers.tserversList,
        context,
        isYbcPresent);
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context,
      boolean isYbcPresent) {
    if (context.processInactiveMaster) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda,
          getInactiveMasters(masterNodes, tServerNodes),
          ServerType.MASTER,
          context,
          false,
          isYbcPresent);
    }

    if (context.processTServersFirst) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true, isYbcPresent);
    }

    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda, masterNodes, ServerType.MASTER, context, true, isYbcPresent);

    if (!context.processTServersFirst) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true, isYbcPresent);
    }
  }

  private List<NodeDetails> getInactiveMasters(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    Universe universe = getUniverse();
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    return tServerNodes != null
        ? tServerNodes.stream()
            .filter(node -> node.placementUuid.equals(primaryClusterUuid))
            .filter(node -> masterNodes == null || !masterNodes.contains(node))
            .collect(Collectors.toList())
        : null;
  }

  private void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeContext context,
      boolean activeRole,
      boolean isYbcPresent) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();

    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);

    if (context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, Collections.singleton(processType));
    }

    if (isYbcPresent) {
      createServerControlTasks(nodes, ServerType.CONTROLLER, "stop")
          .setSubTaskGroupType(subGroupType);
    }
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);

    if (!context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, Collections.singleton(processType));
    }

    if (activeRole) {
      createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(nodes, processType)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      if (isYbcPresent) {
        createServerControlTasks(nodes, ServerType.CONTROLLER, "start")
            .setSubTaskGroupType(subGroupType);
        createWaitForYbcServerTask(new HashSet<NodeDetails>(nodes))
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      // If there are no universe keys on the universe, it will have no effect.
      if (EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
        createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
    }
    if (context.postAction != null) {
      nodes.forEach(context.postAction);
    }
    createSetNodeStateTasks(nodes, NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      MastersAndTservers mastersAndTServers,
      UpgradeContext context) {
    createNonRestartUpgradeTaskFlow(
        nonRestartUpgradeLambda,
        mastersAndTServers.mastersList,
        mastersAndTServers.tserversList,
        context);
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context) {

    if (context.processTServersFirst) {
      createNonRestartUpgradeTaskFlow(
          nonRestartUpgradeLambda, tServerNodes, ServerType.TSERVER, context);
    }

    createNonRestartUpgradeTaskFlow(
        nonRestartUpgradeLambda, masterNodes, ServerType.MASTER, context);

    if (!context.processTServersFirst) {
      createNonRestartUpgradeTaskFlow(
          nonRestartUpgradeLambda, tServerNodes, ServerType.TSERVER, context);
    }
  }

  protected void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeContext context) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    nonRestartUpgradeLambda.run(nodes, Collections.singleton(processType));
    if (context.postAction != null) {
      nodes.forEach(context.postAction);
    }
    createSetNodeStateTasks(nodes, NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  public void createRestartTasks(
      MastersAndTservers mastersAndTServers, UpgradeOption upgradeOption, boolean isYbcPresent) {
    createRestartTasks(
        mastersAndTServers.mastersList,
        mastersAndTServers.tserversList,
        upgradeOption,
        isYbcPresent);
  }

  private void createRestartTasks(
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeOption upgradeOption,
      boolean isYbcPresent) {
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE
        && upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new IllegalArgumentException("Restart can only be either rolling or non-rolling");
    }

    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      createRollingUpgradeTaskFlow(
          (nodes, processType) -> {}, masterNodes, tServerNodes, DEFAULT_CONTEXT, isYbcPresent);
    } else {
      createNonRollingUpgradeTaskFlow(
          (nodes, processType) -> {}, masterNodes, tServerNodes, DEFAULT_CONTEXT, isYbcPresent);
    }
  }

  protected SubTaskGroup createClusterUserIntentUpdateTask(UUID clutserUUID, UUID imageBundleUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateClusterUserIntent");
    UpdateClusterUserIntent.Params updateClusterUserIntentParams =
        new UpdateClusterUserIntent.Params();
    updateClusterUserIntentParams.setUniverseUUID(taskParams().getUniverseUUID());
    updateClusterUserIntentParams.clusterUUID = clutserUUID;
    updateClusterUserIntentParams.imageBundleUUID = imageBundleUUID;

    UpdateClusterUserIntent updateClusterUserIntentTask = createTask(UpdateClusterUserIntent.class);
    updateClusterUserIntentTask.initialize(updateClusterUserIntentParams);
    updateClusterUserIntentTask.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(updateClusterUserIntentTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected AnsibleConfigureServers getAnsibleConfigureServerTask(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      NodeDetails node,
      ServerType processType,
      Map<String, String> oldGflags,
      Map<String, String> newGflags,
      UniverseTaskParams.CommunicationPorts communicationPorts) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);
    params.gflags = newGflags;
    params.gflagsToRemove = GFlagsUtil.getDeletedGFlags(oldGflags, newGflags);
    if (communicationPorts != null) {
      params.communicationPorts = communicationPorts;
      params.overrideNodePorts = true;
    }
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  protected void createServerConfFileUpdateTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes,
      UniverseDefinitionTaskParams.Cluster curCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> curClusters,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> newClusters) {
    createServerConfFileUpdateTasks(
        userIntent,
        nodes,
        processTypes,
        curCluster,
        curClusters,
        newCluster,
        newClusters,
        null /* communicationPorts */);
  }

  /**
   * Create a task to update server conf files on DB nodes.
   *
   * @param userIntent modified user intent of the current cluster.
   * @param nodes set of nodes on which the file needs to be updated.
   * @param processTypes set of processes whose conf files need to be updated.
   * @param curCluster current cluster.
   * @param curClusters set of current clusters.
   * @param newCluster updated new cluster.
   * @param newClusters set of updated new clusters.
   * @param communicationPorts new communication ports on DB nodes.
   */
  protected void createServerConfFileUpdateTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes,
      UniverseDefinitionTaskParams.Cluster curCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> curClusters,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> newClusters,
      UniverseTaskParams.CommunicationPorts communicationPorts) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    TaskExecutor.SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      ServerType processType = getSingle(processTypes);
      Map<String, String> newGFlags =
          GFlagsUtil.getGFlagsForNode(node, processType, newCluster, newClusters);
      Map<String, String> oldGFlags =
          GFlagsUtil.getGFlagsForNode(node, processType, curCluster, curClusters);
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              userIntent, node, processType, oldGFlags, newGFlags, communicationPorts));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected void createManageOtelCollectorTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      boolean installOtelCollector,
      AuditLogConfig auditLogConfig,
      Function<NodeDetails, Map<String, String>> nodeToGflags) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.ManageOtelCollector, taskParams().nodePrefix);
    TaskExecutor.SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      ManageOtelCollector.Params params = new ManageOtelCollector.Params();
      params.nodeName = node.nodeName;
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.azUuid = node.azUuid;
      params.installOtelCollector = installOtelCollector;
      params.otelCollectorEnabled =
          installOtelCollector || getUniverse().getUniverseDetails().otelCollectorEnabled;
      params.auditLogConfig = auditLogConfig;
      params.deviceInfo = userIntent.getDeviceInfoForNode(node);
      params.gflags = nodeToGflags.apply(node);

      ManageOtelCollector task = createTask(ManageOtelCollector.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ManageOtelCollector);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected void checkForbiddenToOverrideGFlags(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Universe universe,
      ServerType processType,
      Map<String, String> newGFlags) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);

    String errorMsg =
        GFlagsUtil.checkForbiddenToOverride(
            node, params, userIntent, universe, newGFlags, config, confGetter);
    if (errorMsg != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          errorMsg
              + ". It is not advised to set these internal gflags. If you want to do it"
              + " forcefully - set runtime config value for "
              + "'yb.gflags.allow_user_override' to 'true'");
    }
  }

  protected ServerType getSingle(Set<ServerType> processTypes) {
    Set<ServerType> filteredServerTypes = new HashSet<>();
    for (ServerType serverType : processTypes) {
      if (!canBeIgnoredServerTypes.contains(serverType)) {
        filteredServerTypes.add(serverType);
      }
    }
    if (filteredServerTypes.size() != 1) {
      throw new IllegalArgumentException("Expected to have single element, got " + processTypes);
    }
    return filteredServerTypes.iterator().next();
  }

  private List<NodeDetails> filterForClusters(List<NodeDetails> nodes) {
    Set<UUID> clusterUUIDs =
        taskParams().clusters.stream().map(c -> c.uuid).collect(Collectors.toSet());
    return nodes.stream()
        .filter(n -> clusterUUIDs.contains(n.placementUuid))
        .collect(Collectors.toList());
  }

  protected MastersAndTservers fetchNodesForClustersInParams() {
    Universe universe = getUniverse();
    return new MastersAndTservers(
        filterForClusters(fetchMasterNodes(universe, taskParams().upgradeOption)),
        filterForClusters(fetchTServerNodes(universe, taskParams().upgradeOption)));
  }

  public LinkedHashSet<NodeDetails> fetchNodesForCluster() {
    return toOrderedSet(fetchNodesForClustersInParams().asPair());
  }

  public LinkedHashSet<NodeDetails> fetchAllNodes(UpgradeOption upgradeOption) {
    return toOrderedSet(fetchNodes(upgradeOption).asPair());
  }

  protected MastersAndTservers fetchNodes(UpgradeOption upgradeOption) {
    return new MastersAndTservers(
        fetchMasterNodes(upgradeOption), fetchTServerNodes(upgradeOption));
  }

  public List<NodeDetails> fetchMasterNodes(UpgradeOption upgradeOption) {
    return fetchMasterNodes(getUniverse(), upgradeOption);
  }

  private List<NodeDetails> fetchMasterNodes(Universe universe, UpgradeOption upgradeOption) {
    List<NodeDetails> masterNodes = universe.getMasters();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      final String leaderMasterAddress = universe.getMasterLeaderHostText();
      return sortMastersInRestartOrder(leaderMasterAddress, masterNodes);
    }
    return masterNodes;
  }

  public List<NodeDetails> fetchTServerNodes(UpgradeOption upgradeOption) {
    return fetchTServerNodes(getUniverse(), upgradeOption);
  }

  private List<NodeDetails> fetchTServerNodes(Universe universe, UpgradeOption upgradeOption) {
    List<NodeDetails> tServerNodes = universe.getTServers();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      return sortTServersInRestartOrder(universe, tServerNodes);
    }
    return tServerNodes;
  }

  public int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER
        ? taskParams().sleepAfterMasterRestartMillis
        : taskParams().sleepAfterTServerRestartMillis;
  }

  // Find the master leader and move it to the end of the list.
  public static List<NodeDetails> sortMastersInRestartOrder(
      String leaderMasterAddress, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }
    return nodes.stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(node -> node.state == NodeState.Live)
                .thenComparing(node -> leaderMasterAddress.equals(node.cloudInfo.private_ip))
                .thenComparing(NodeDetails::getNodeIdx))
        .collect(Collectors.toList());
  }

  private static List<UUID> sortAZs(
      UniverseDefinitionTaskParams.Cluster cluster, Universe universe) {
    List<UUID> result = new ArrayList<>();
    Map<UUID, Integer> indexByUUID = new HashMap<>();
    universe.getNodesInCluster(cluster.uuid).stream()
        .sorted(Comparator.comparing(NodeDetails::getNodeIdx))
        .forEach(n -> indexByUUID.putIfAbsent(n.getAzUuid(), n.getNodeIdx()));

    cluster
        .placementInfo
        .azStream()
        .sorted(
            Comparator.<PlacementAZ, Boolean>comparing(az -> !az.isAffinitized)
                .thenComparing(az -> az.numNodesInAZ)
                // To keep predictability in tests, we sort zones by the order of nodes in universe.
                .thenComparing(az -> indexByUUID.getOrDefault(az.uuid, Integer.MAX_VALUE))
                .thenComparing(az -> az.uuid))
        .forEach(az -> result.add(az.uuid));
    return result;
  }

  // Find the master leader and move it to the end of the list.
  public static List<NodeDetails> sortTServersInRestartOrder(
      Universe universe, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    Map<UUID, List<UUID>> sortedAZsByCluster = new HashMap<>();
    sortedAZsByCluster.put(
        primaryClusterUuid, sortAZs(universe.getUniverseDetails().getPrimaryCluster(), universe));
    for (UniverseDefinitionTaskParams.Cluster cl :
        universe.getUniverseDetails().getReadOnlyClusters()) {
      sortedAZsByCluster.put(cl.uuid, sortAZs(cl, universe));
    }
    return nodes.stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(
                    // Fully upgrade primary cluster first
                    node -> !node.placementUuid.equals(primaryClusterUuid))
                .thenComparing(node -> node.state == NodeState.Live)
                .thenComparing(
                    node -> {
                      Integer azIdx =
                          sortedAZsByCluster.get(node.placementUuid).indexOf(node.azUuid);
                      return azIdx < 0 ? Integer.MAX_VALUE : azIdx;
                    })
                .thenComparing(NodeDetails::getNodeIdx))
        .collect(Collectors.toList());
  }

  // Get the TriggerType for the given situation and trigger the hooks
  private void createHookTriggerTasks(
      Collection<NodeDetails> nodes, boolean isPre, boolean isRolling) {
    String className = this.getClass().getSimpleName();
    if (this.getClass().equals(SoftwareUpgradeYB.class)) {
      // use same hook for new upgrade task which was added for old upgrade task.
      className = SoftwareUpgrade.class.getSimpleName();
    }
    String triggerName = (isPre ? "Pre" : "Post") + className;
    if (isRolling) triggerName += "NodeUpgrade";
    Optional<TriggerType> optTrigger = TriggerType.maybeResolve(triggerName);
    if (optTrigger.isPresent())
      HookInserter.addHookTrigger(optTrigger.get(), this, taskParams(), nodes);
  }

  @Value
  @Builder
  public static class UpgradeContext {
    boolean reconfigureMaster;
    boolean runBeforeStopping;
    boolean processInactiveMaster;
    @Builder.Default boolean processTServersFirst = false;
    // Set this field to access client userIntent during runtime as
    // usually universeDetails are updated only at the end of task.
    UniverseDefinitionTaskParams.UserIntent userIntent;
    // Set this field to provide custom communication ports during runtime.
    CommunicationPorts communicationPorts;
    @Builder.Default boolean skipStartingProcesses = false;
    String targetSoftwareVersion;
    Consumer<NodeDetails> postAction;
  }
}
