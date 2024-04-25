// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Stopwatch;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.common.collect.Streams;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.PortType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupPreflightValidate;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTable;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTableYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTableYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupUniverseKeys;
import com.yugabyte.yw.commissioner.tasks.subtasks.BulkImport;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeAdminPassword;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckFollowerLag;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckNodeSafeToDelete;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateAlertDefinitions;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTable;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteDrConfigEntry;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteKeyspace;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTablesFromUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.DestroyEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.DisableEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.EnableEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.FreezeUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.HardRebootServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallNodeAgent;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallYbcSoftwareOnK8s;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageAlertDefinitions;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageLoadBalancerGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManipulateDnsRecordTask;
import com.yugabyte.yw.commissioner.tasks.subtasks.MarkUniverseForHealthScriptReUpload;
import com.yugabyte.yw.commissioner.tasks.subtasks.ModifyBlackList;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.PauseServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.PersistResizeNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.PersistSystemdUpgrade;
import com.yugabyte.yw.commissioner.tasks.subtasks.PromoteAutoFlags;
import com.yugabyte.yw.commissioner.tasks.subtasks.RebootServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResetUniverseVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreBackupYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreBackupYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestorePreflightValidate;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeys;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeysYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeysYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResumeServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.RollbackAutoFlags;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunYsqlUpgrade;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetActiveUniverseKeys;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetFlagInMemory;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeStatus;
import com.yugabyte.yw.commissioner.tasks.subtasks.StoreAutoFlagConfigVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.SwamperTargetsFileUpdate;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.commissioner.tasks.subtasks.UnivSetCertificate;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateSucceeded;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistGFlags;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateMountedDisks;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateSoftwareVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseSoftwareUpgradeState;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseYbcDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseYbcGflagsDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpgradeYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForClockSync;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDuration;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForEncryptionKeyInMemory;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLeaderBlacklistCompletion;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLeadersOnPreferredOnly;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForNodeAgent;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServerReady;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForTServerHeartBeats;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForYbcServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.YBCBackupSucceeded;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckGlibc;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckLocale;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckMemory;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckSoftwareVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckUpgrade;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckXUniverseAutoFlags;
import com.yugabyte.yw.commissioner.tasks.subtasks.nodes.UpdateNodeProcess;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ChangeXClusterRole;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteBootstrapIds;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteReplication;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteXClusterConfigEntry;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteXClusterTableConfigEntry;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.PromoteSecondaryConfigToMainConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ResetXClusterConfigEntry;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetDrStates;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigUpdateMasterAddresses;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterInfoPersist;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.DrConfigStates;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.UniverseInProgressException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupNodeRetriever;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.Universe.UniverseUpdaterConfig;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.helpers.ClusterAZ;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.ColumnDetails.YQLDataType;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TableDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.Builder;
import lombok.Getter;
import lombok.Singular;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.MDC;
import org.yb.ColumnSchema.SortOrder;
import org.yb.CommonTypes;
import org.yb.CommonTypes.TableType;
import org.yb.cdc.CdcConsumer.XClusterRole;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.ListNamespacesResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.ModifyClusterConfigIncrementVersion;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.ServerInfo;
import org.yb.util.TabletServerInfo;
import play.libs.Json;

@Slf4j
public abstract class UniverseTaskBase extends AbstractTaskBase {

  @Builder
  @Getter
  public static class AllowedTasks {
    private boolean restricted;
    private TaskType lockedTaskType;
    // Allowed task types.
    @Singular private Set<TaskType> taskTypes;

    public static AllowedTasks.AllowedTasksBuilder builder() {
      return new CustomBuilder();
    }

    private static class CustomBuilder extends AllowedTasks.AllowedTasksBuilder {
      @Override
      public CustomBuilder taskTypes(Collection<? extends TaskType> taskTypes) {
        taskTypes.stream().forEach(t -> super.taskType(t));
        return this;
      }
    }
  }

  // Tasks that modify cluster placement.
  // If one of such tasks is failed, we should not allow starting most of other tasks,
  // until failed task is retried.
  private static final Set<TaskType> PLACEMENT_MODIFICATION_TASKS =
      ImmutableSet.of(
          TaskType.CreateUniverse,
          TaskType.CreateKubernetesUniverse,
          TaskType.ReadOnlyClusterCreate,
          TaskType.EditUniverse,
          TaskType.AddNodeToUniverse,
          TaskType.RemoveNodeFromUniverse,
          TaskType.DeleteNodeFromUniverse,
          TaskType.EditUniverse,
          TaskType.ReplaceNodeInUniverse,
          TaskType.ReleaseInstanceFromUniverse,
          TaskType.StartNodeInUniverse,
          TaskType.StopNodeInUniverse,
          TaskType.ResizeNode,
          TaskType.KubernetesOverridesUpgrade,
          TaskType.GFlagsKubernetesUpgrade,
          TaskType.SoftwareKubernetesUpgrade,
          TaskType.SoftwareKubernetesUpgradeYB,
          TaskType.EditKubernetesUniverse,
          TaskType.RestartUniverseKubernetesUpgrade,
          TaskType.CertsRotateKubernetesUpgrade,
          TaskType.GFlagsUpgrade,
          TaskType.SoftwareUpgrade,
          TaskType.SoftwareUpgradeYB,
          TaskType.FinalizeUpgrade,
          TaskType.RollbackUpgrade,
          TaskType.RollbackKubernetesUpgrade,
          TaskType.RestartUniverse,
          TaskType.RebootNodeInUniverse,
          TaskType.VMImageUpgrade,
          TaskType.ThirdpartySoftwareUpgrade,
          TaskType.CertsRotate);

  // Tasks that are allowed to run if cluster placement modification task failed.
  // This mapping blocks/allows actions on the UI done by a mapping defined in
  // UNIVERSE_ACTION_TO_FROZEN_TASK_MAP in "./managed/ui/src/redesign/helpers/constants.ts".
  private static final Set<TaskType> SAFE_TO_RUN_IF_UNIVERSE_BROKEN =
      ImmutableSet.of(
          TaskType.CreateBackup,
          TaskType.BackupUniverse,
          TaskType.MultiTableBackup,
          TaskType.RestoreBackup,
          TaskType.CreatePitrConfig,
          TaskType.DeletePitrConfig,
          TaskType.CreateXClusterConfig,
          TaskType.EditXClusterConfig,
          TaskType.DeleteXClusterConfig,
          TaskType.RestartXClusterConfig,
          TaskType.SyncXClusterConfig,
          TaskType.DestroyUniverse,
          TaskType.DestroyKubernetesUniverse,
          TaskType.ReinstallNodeAgent,
          TaskType.ReadOnlyClusterDelete,
          TaskType.CreateSupportBundle);

  private static final Set<TaskType> RERUNNABLE_PLACEMENT_MODIFICATION_TASKS =
      ImmutableSet.of(
          TaskType.GFlagsUpgrade,
          TaskType.RestartUniverse,
          TaskType.VMImageUpgrade,
          TaskType.GFlagsKubernetesUpgrade,
          TaskType.KubernetesOverridesUpgrade,
          TaskType.EditKubernetesUniverse /* Partially allowing this for resource spec changes */);

  private static final Set<TaskType> SOFTWARE_UPGRADE_ROLLBACK_TASKS =
      ImmutableSet.of(TaskType.RollbackKubernetesUpgrade, TaskType.RollbackUpgrade);

  private static final Set<TaskType> ROLLBACK_SUPPORTED_SOFTWARE_UPGRADE_TASKS =
      ImmutableSet.of(TaskType.SoftwareKubernetesUpgradeYB, TaskType.SoftwareUpgradeYB);

  protected Set<UUID> lockedXClusterUniversesUuidSet = null;

  protected static final String MIN_WRITE_READ_TABLE_CREATION_RELEASE = "2.6.0.0";

  @VisibleForTesting static final Duration SLEEP_TIME_FORCE_LOCK_RETRY = Duration.ofSeconds(10);

  protected String ysqlPassword;
  protected String ycqlPassword;
  private String ysqlCurrentPassword = Util.DEFAULT_YSQL_PASSWORD;
  private String ysqlUsername = Util.DEFAULT_YSQL_USERNAME;
  private String ycqlCurrentPassword = Util.DEFAULT_YCQL_PASSWORD;
  private String ycqlUsername = Util.DEFAULT_YCQL_USERNAME;
  private String ysqlDb = Util.YUGABYTE_DB;

  protected YbcBackupNodeRetriever ybcBackupNodeRetriever;

  public enum VersionCheckMode {
    NEVER,
    ALWAYS,
    HA_ONLY
  }

  // Enum for specifying the server type.
  public enum ServerType {
    MASTER,
    TSERVER,
    CONTROLLER,
    // TODO: Replace all YQLServer with YCQLserver
    YQLSERVER,
    YSQLSERVER,
    REDISSERVER,
    EITHER
  }

  public static final String DUMP_ENTITIES_URL_SUFFIX = "/dump-entities";

  @Inject
  protected UniverseTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  private final AtomicReference<ExecutionContext> executionContext = new AtomicReference<>();

  public class ExecutionContext {
    private final UUID universeUuid;
    private final boolean blacklistLeaders;
    private final int leaderBacklistWaitTimeMs;
    private final Duration waitForServerReadyTimeout;
    private final boolean followerLagCheckEnabled;
    private boolean loadBalancerOff = false;
    private final Set<UUID> lockedUniversesUuid = ConcurrentHashMap.newKeySet();
    private final AtomicReference<Set<NodeDetails>> masterNodes = new AtomicReference<>();

    ExecutionContext() {
      this.universeUuid = taskParams().getUniverseUUID();
      Universe universe = Universe.getOrBadRequest(this.universeUuid);
      blacklistLeaders =
          confGetter.getConfForScope(universe, UniverseConfKeys.ybUpgradeBlacklistLeaders);

      leaderBacklistWaitTimeMs =
          confGetter.getConfForScope(universe, UniverseConfKeys.ybUpgradeBlacklistLeaderWaitTimeMs);

      followerLagCheckEnabled =
          confGetter.getConfForScope(universe, UniverseConfKeys.followerLagCheckEnabled);

      waitForServerReadyTimeout =
          confGetter.getConfForScope(universe, UniverseConfKeys.waitForServerReadyTimeout);
    }

    public boolean isLoadBalancerOff() {
      return loadBalancerOff;
    }

    public boolean isBlacklistLeaders() {
      return blacklistLeaders;
    }

    public boolean isFollowerLagCheckEnabled() {
      return followerLagCheckEnabled;
    }

    public Duration getWaitForServerReadyTimeout() {
      return waitForServerReadyTimeout;
    }

    public void lockUniverse(UUID universeUUID) {
      lockedUniversesUuid.add(universeUUID);
    }

    public boolean isLocked(UUID universeUUID) {
      return lockedUniversesUuid.contains(universeUUID);
    }

    public void unlockUniverse(UUID universeUUID) {
      lockedUniversesUuid.remove(universeUUID);
    }

    public void setMasterNodes(Set<NodeDetails> nodes) {
      masterNodes.set(ImmutableSet.copyOf(Objects.requireNonNull(nodes)));
    }

    // A supplier is evaluated late when the subtask is run. Initially, when a subtask is created
    // the node may not have IP.
    public Supplier<String> getMasterAddrsSupplier() {
      // Take the current set of the masters when this is invoked.
      final Set<NodeDetails> nodes = masterNodes.get();
      if (CollectionUtils.isEmpty(nodes)) {
        return null;
      }
      return () -> {
        // Refresh the nodes from the DB to get IPs.
        Universe universe = Universe.getOrBadRequest(universeUuid);
        return universe.getHostPortsString(
            universe.getNodes().stream().filter(n -> nodes.contains(n)).collect(Collectors.toSet()),
            ServerType.MASTER,
            PortType.RPC);
      };
    }

    public void removeMasterNode(NodeDetails node) {
      masterNodes.getAndUpdate(
          v -> {
            if (v != null) {
              Set<NodeDetails> nodes = new HashSet<>(v);
              nodes.remove(node);
              return Collections.unmodifiableSet(nodes);
            }
            return null;
          });
    }

    public void addMasterNode(NodeDetails node) {
      masterNodes.getAndUpdate(
          v -> {
            Set<NodeDetails> nodes = v == null ? new HashSet<>() : new HashSet<>(v);
            nodes.add(node);
            return Collections.unmodifiableSet(nodes);
          });
    }
  }

  // The task params.
  @Override
  protected UniverseTaskParams taskParams() {
    return (UniverseTaskParams) taskParams;
  }

  protected Consumer<Universe> getAdditionalValidator() {
    TaskType taskType = getTaskExecutor().getTaskType(getClass());
    Consumer<Universe> releaseValidator =
        universe -> {
          if (!SAFE_TO_RUN_IF_UNIVERSE_BROKEN.contains(taskType)
              && confGetter.getConfForScope(universe, UniverseConfKeys.validateLocalRelease)) {
            if (!validateLocalFilepath(
                universe,
                releaseManager.getReleaseByVersion(
                    universe
                        .getUniverseDetails()
                        .getPrimaryCluster()
                        .userIntent
                        .ybSoftwareVersion))) {
              throw new PlatformServiceException(
                  INTERNAL_SERVER_ERROR, "Error validating local release for universe.");
            }
          }
        };
    return releaseValidator;
  }

  public static boolean validateLocalFilepath(Universe universe, ReleaseContainer release) {
    if (release == null) {
      String msg =
          String.format("Universe %s does not have valid metadata.", universe.getUniverseUUID());
      log.error(msg);
      return false;
    }
    Set<String> localFilePaths = release.getLocalReleasePathStrings();
    for (String path : localFilePaths) {
      Path localPath = Paths.get(path);
      if (!Files.exists(localPath)) {
        String msg =
            String.format(
                "Could not find path %s on system for YB software version %s",
                localPath, release.getVersion());
        log.error(msg);
        return false;
      }
    }
    return true;
  }

  public static AllowedTasks getAllowedTasksOnFailure(TaskInfo placementModificationTaskInfo) {
    TaskType lockedTaskType = placementModificationTaskInfo.getTaskType();
    AllowedTasks.AllowedTasksBuilder builder =
        AllowedTasks.builder().lockedTaskType(lockedTaskType);
    if (PLACEMENT_MODIFICATION_TASKS.contains(lockedTaskType)) {
      builder.restricted(true);
      builder.taskTypes(SAFE_TO_RUN_IF_UNIVERSE_BROKEN);
      if (ROLLBACK_SUPPORTED_SOFTWARE_UPGRADE_TASKS.contains(lockedTaskType)) {
        builder.taskTypes(SOFTWARE_UPGRADE_ROLLBACK_TASKS);
      }
      if (RERUNNABLE_PLACEMENT_MODIFICATION_TASKS.contains(lockedTaskType)) {
        switch (lockedTaskType) {
          case EditKubernetesUniverse:
            if (EditKubernetesUniverse.checkEditKubernetesRerunAllowed(
                placementModificationTaskInfo)) {
              builder.taskType(lockedTaskType);
            }
            break;
          default:
            builder.taskType(lockedTaskType);
        }
      }
    }
    return builder.build();
  }

  /**
   * Returns the allowed tasks object when the universe is in a frozen failed state.
   *
   * @param lockedTaskType the task which froze the universe and failed.
   * @return the allowed tasks.
   */
  public static AllowedTasks getAllowedTasksOnFailure(TaskType lockedTaskType) {
    AllowedTasks.AllowedTasksBuilder builder =
        AllowedTasks.builder().lockedTaskType(lockedTaskType);
    if (PLACEMENT_MODIFICATION_TASKS.contains(lockedTaskType)) {
      builder.restricted(true);
      builder.taskTypes(SAFE_TO_RUN_IF_UNIVERSE_BROKEN);
      if (RERUNNABLE_PLACEMENT_MODIFICATION_TASKS.contains(lockedTaskType)) {
        // Allow only this task.
        builder.taskType(lockedTaskType);
      }
      if (ROLLBACK_SUPPORTED_SOFTWARE_UPGRADE_TASKS.contains(lockedTaskType)) {
        builder.taskTypes(SOFTWARE_UPGRADE_ROLLBACK_TASKS);
      }
    }
    return builder.build();
  }

  /**
   * Returns the allowed task object when the universe is in a frozen failed state.
   *
   * @param lockedPlacementModificationTaskUuid the placement modification task UUID.
   * @return the allowed tasks.
   */
  public static AllowedTasks getAllowedTasksOnFailure(UUID lockedPlacementModificationTaskUuid) {
    if (lockedPlacementModificationTaskUuid == null) {
      return AllowedTasks.builder().build();
    }
    Optional<TaskInfo> optional = TaskInfo.maybeGet(lockedPlacementModificationTaskUuid);
    if (!optional.isPresent()) {
      // Just log a message as this should not happen.
      log.warn("Task info record is not found for {}", lockedPlacementModificationTaskUuid);
      return AllowedTasks.builder()
          .restricted(true)
          .taskTypes(SAFE_TO_RUN_IF_UNIVERSE_BROKEN)
          .build();
    }
    return getAllowedTasksOnFailure(optional.get().getTaskType());
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    TaskType taskType = getTaskExecutor().getTaskType(getClass());
    if (taskType == null) {
      String msg = "TaskType not found for class " + getClass().getCanonicalName();
      log.error(msg);
      throw new IllegalStateException(msg);
    }
    if (taskParams().getUniverseUUID() != null) {
      Universe.maybeGet(taskParams().getUniverseUUID())
          .ifPresent(
              universe -> {
                if (isFirstTry) {
                  UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
                  AllowedTasks allowedTasks =
                      getAllowedTasksOnFailure(universeDetails.placementModificationTaskUuid);
                  boolean isSafeToRun =
                      !allowedTasks.isRestricted()
                          || allowedTasks.getTaskTypes().contains(taskType);
                  if (!isSafeToRun) {
                    String msg =
                        String.format(
                            "Universe %s placement update failed - can't run %s task until"
                                + " placement update succeeds",
                            universe.getUniverseUUID(), taskType.name());
                    log.error(msg);
                    throw new RuntimeException(msg);
                  }
                  Consumer<Universe> validator = getAdditionalValidator();
                  if (validator != null) {
                    validator.accept(universe);
                  }
                }
                validateUniverseState(universe);
              });
    }
  }

  /**
   * Override this to perform additional universe state check in addition to {@link
   * #validateParams(boolean)}.
   */
  protected void validateUniverseState(Universe universe) {
    TaskType taskType = getTaskExecutor().getTaskType(getClass());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    boolean isResumeOrDelete =
        (taskType == TaskType.ResumeUniverse || taskType == TaskType.DestroyUniverse);
    if (universeDetails.universePaused && !isResumeOrDelete) {
      String msg = "Universe " + universe.getUniverseUUID() + " is currently paused";
      log.error(msg);
      throw new RuntimeException(msg);
    }
    // If this universe is already being edited, fail the request.
    if (universeDetails.updateInProgress) {
      String msg = "Universe " + universe.getUniverseUUID() + " is already being updated";
      log.error(msg);
      throw new UniverseInProgressException(msg);
    }
  }

  /**
   * This is first invoked with the universe to create long running async validation subtasks after
   * the universe is locked.
   *
   * @param universe the locked universe.
   */
  protected void createPrecheckTasks(Universe universe) {}

  protected Universe getUniverse() {
    return Universe.getOrBadRequest(taskParams().getUniverseUUID());
  }

  protected ExecutionContext getOrCreateExecutionContext() {
    if (!getUserTaskUUID().equals(getTaskUUID())) {
      log.warn(
          "Execution context is getting created for subtasks {} in task {}",
          getUserTaskUUID(),
          getTaskUUID());
    }
    if (executionContext.get() == null) {
      executionContext.compareAndSet(null, new ExecutionContext());
    }
    return executionContext.get();
  }

  protected boolean isLeaderBlacklistValidRF(NodeDetails nodeDetails) {
    Cluster curCluster = getUniverse().getCluster(nodeDetails.placementUuid);
    if (curCluster == null) {
      return false;
    }
    return curCluster.userIntent.replicationFactor > 1;
  }

  protected UserIntent getUserIntent() {
    return getUniverse().getUniverseDetails().getPrimaryCluster().userIntent;
  }

  protected void putDateIntoCache(String key) {
    getTaskCache().put(key, Json.toJson(new Date()));
  }

  protected Date getDateFromCache(String key) {
    JsonNode jsonNode = getTaskCache().get(key);
    if (jsonNode != null) {
      return Json.fromJson(jsonNode, Date.class);
    }
    return null;
  }

  private UniverseUpdater getLockingUniverseUpdater(UniverseUpdaterConfig updaterConfig) {
    TaskType owner = getTaskExecutor().getTaskType(getClass());
    if (owner == null) {
      String msg = "TaskType not found for class " + this.getClass().getCanonicalName();
      log.error(msg);
      throw new IllegalStateException(msg);
    }
    return new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        if (isFirstTry()) {
          // Universe already has a reference to the last task UUID in case of retry.
          // Check version only when it is a first try.
          verifyUniverseVersion(getConfig().getExpectedUniverseVersion(), universe);
        }
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        boolean isResumeOrDelete =
            (owner == TaskType.ResumeUniverse || owner == TaskType.DestroyUniverse);
        if (universeDetails.universePaused && !isResumeOrDelete) {
          String msg = "Universe " + universe.getUniverseUUID() + " is currently paused";
          log.error(msg);
          throw new RuntimeException(msg);
        }
        // If this universe is already being edited, fail the request.
        if (!getConfig().isForceUpdate() && universeDetails.updateInProgress) {
          String msg = "Universe " + universe.getUniverseUUID() + " is already being updated";
          log.error(msg);
          throw new UniverseInProgressException(msg);
        }
        if (taskParams().getPreviousTaskUUID() != null) {
          // If the task is retried, check if the task UUID is same as the one in the universe.
          // Check this condition only on retry to retain same behavior as before.
          boolean isLastTaskOrLastPlacementTaskRetry =
              Objects.equals(taskParams().getPreviousTaskUUID(), universeDetails.updatingTaskUUID)
                  || Objects.equals(
                      taskParams().getPreviousTaskUUID(),
                      universeDetails.placementModificationTaskUuid);
          if (!getConfig().isForceUpdate() && !isLastTaskOrLastPlacementTaskRetry) {
            String msg =
                "Only the last task " + taskParams().getPreviousTaskUUID() + " can be retried";
            log.error(msg);
            throw new RuntimeException(msg);
          }
        } else if (universeDetails.placementModificationTaskUuid != null) {
          // If we're in the middle of placement modification task (failed and waiting to be
          // retried), only allow subset of safe to execute tasks.
          AllowedTasks allowedTasks =
              getAllowedTasksOnFailure(universeDetails.placementModificationTaskUuid);
          boolean isSafeToRun =
              !allowedTasks.isRestricted() || allowedTasks.getTaskTypes().contains(owner);
          if (!isSafeToRun) {
            String msg =
                "Universe "
                    + universe.getUniverseUUID()
                    + " placement update failed - can't run "
                    + owner.name()
                    + " task until placement update succeeds";
            log.error(msg);
            throw new RuntimeException(msg);
          }
        }
        markUniverseUpdateInProgress(owner, universe, getConfig());
      }

      @Override
      public UniverseUpdaterConfig getConfig() {
        return updaterConfig;
      }
    };
  }

  protected UniverseUpdater getFreezeUniverseUpdater(UniverseUpdaterConfig updaterConfig) {
    TaskType owner = getRunnableTask().getTaskInfo().getTaskType();
    if (owner == null) {
      String msg = "User task is not found for class " + this.getClass().getCanonicalName();
      log.error(msg);
      throw new IllegalStateException(msg);
    }
    return new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (!universeDetails.updateInProgress) {
          String msg = "Universe " + universe.getUniverseUUID() + " is not being updated";
          log.error(msg);
          throw new IllegalStateException(msg);
        }
        if (getUserTaskUUID().equals(universeDetails.updatingTaskUUID)) {
          // Freeze always sets this to the UUID of the currently run task. If it is already set to
          // the current task UUID, freeze is already run for this task.
          String msg = "Universe " + universe.getUniverseUUID() + " is already frozen";
          log.error(msg);
          throw new IllegalStateException(msg);
        }
        markUniverseUpdateInProgress(owner, universe, getConfig());
      }

      @Override
      public UniverseUpdaterConfig getConfig() {
        return updaterConfig;
      }
    };
  }

  private void markUniverseUpdateInProgress(
      TaskType owner, Universe universe, UniverseUpdaterConfig updaterConfig) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    // This locks the universe.
    universeDetails.updateInProgress = true;
    if (updaterConfig.isFreezeUniverse()) {
      universeDetails.updatingTask = owner;
      universeDetails.updatingTaskUUID = getUserTaskUUID();
      if (PLACEMENT_MODIFICATION_TASKS.contains(owner)) {
        universeDetails.placementModificationTaskUuid = getUserTaskUUID();
      }
      if (updaterConfig.isCheckSuccess()) {
        universeDetails.updateSucceeded = false;
      }
      universe.setUniverseDetails(universeDetails);
      Consumer<Universe> callback = updaterConfig.getCallback();
      if (callback != null) {
        callback.accept(universe);
      }
    }
  }

  /**
   * verifyUniverseVersion
   *
   * @param universe
   *     <p>This is attempting to flag situations where the UI is operating on a stale copy of the
   *     universe for example, when multiple browsers or users are operating on the same universe.
   *     <p>This assumes that the UI supplies the expectedUniverseVersion in the API call but this
   *     is not always true. If the UI does not supply it, expectedUniverseVersion is set from
   *     universe.version itself so this check is not useful in that case.
   */
  public void verifyUniverseVersion(int expectedUniverseVersion, Universe universe) {
    if (expectedUniverseVersion != -1 && expectedUniverseVersion != universe.getVersion()) {
      String msg =
          "Universe "
              + taskParams().getUniverseUUID()
              + " version "
              + universe.getVersion()
              + ", is different from the expected version of "
              + expectedUniverseVersion
              + ". User "
              + "would have to sumbit the operation from a refreshed top-level universe page.";
      log.error(msg);
      throw new IllegalStateException(msg);
    }
  }

  // Abort the ongoing task on the universe and grab the lock forcefully.
  private Universe lockUniversePremptively(
      Universe universe, long timeoutSecs, UniverseUpdater updater) {
    // Ensure that the force update is set.
    updater.getConfig().setForceUpdate(true);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (universeDetails.updatingTaskUUID != null) {
      log.info(
          "Lock preempively universe {} at version {}.",
          universe.getUniverseUUID(),
          updater.getConfig().getExpectedUniverseVersion());
      // Abort the current task if it is running before grabbing the lock on this universe.
      getCommissioner().abortTask(universeDetails.updatingTaskUUID, true);
      Stopwatch stopwatch = Stopwatch.createStarted();
      // The task will always abort. This timeout loop is just for safety.
      while (getCommissioner().isTaskRunning(universeDetails.updatingTaskUUID)) {
        if (stopwatch.elapsed(TimeUnit.SECONDS) > timeoutSecs) {
          String msg = "Universe " + universe.getUniverseUUID() + " could not be aborted in time";
          log.error(msg);
          throw new UniverseInProgressException(msg);
        }
        log.debug("Waiting for running task {} to abort", universeDetails.updatingTaskUUID);
        UniverseTaskBase.this.waitFor(SLEEP_TIME_FORCE_LOCK_RETRY);
      }
    }
    universe = saveUniverseDetails(universe.getUniverseUUID(), updater);
    getOrCreateExecutionContext().lockUniverse(universe.getUniverseUUID());
    log.debug("Locked universe {}", universe.getUniverseUUID());
    return universe;
  }

  private Universe lockUniverseForUpdate(UUID universeUuid, UniverseUpdater updater) {
    if (!updater.getConfig().isForceUpdate()) {
      Universe universe = saveUniverseDetails(universeUuid, updater);
      getOrCreateExecutionContext().lockUniverse(universeUuid);
      log.debug("Locked universe {}", universeUuid);
      return universe;
    }
    log.info(
        "Force lock universe {} at version {}.",
        universeUuid,
        updater.getConfig().getExpectedUniverseVersion());
    Universe universe = Universe.getOrBadRequest(universeUuid);
    long retryNumber = 0;
    long timeoutSecs =
        config.getDuration("yb.task.max_force_universe_lock_timeout", TimeUnit.SECONDS);
    Stopwatch stopwatch = Stopwatch.createStarted();
    while (true) {
      try {
        // This allows switching the runtime flag from wait-retry to preemptive lock in the loop.
        if (confGetter.getConfForScope(universe, UniverseConfKeys.taskOverrideForceUniverseLock)) {
          return lockUniversePremptively(universe, timeoutSecs, updater);
        }
        // TODO Need to investigate the effectiveness of the wait below in real scenario.
        // Override force locking to false and retry.
        updater.getConfig().setForceUpdate(false);
        universe = saveUniverseDetails(universeUuid, updater);
        getOrCreateExecutionContext().lockUniverse(universeUuid);
        log.debug("Locked universe {}", universeUuid);
        return universe;
      } catch (UniverseInProgressException e) {
        if (updater.getConfig().isForceUpdate()
            || stopwatch.elapsed(TimeUnit.SECONDS) > timeoutSecs) {
          // If it is either preemptive lock or time is up, throw the exception.
          throw e;
        }
        retryNumber++;
        log.warn(
            "Universe {} was locked: {}; retrying after {} seconds. Completed attempt {}",
            universeUuid,
            e.getMessage(),
            SLEEP_TIME_FORCE_LOCK_RETRY.getSeconds(),
            retryNumber);
      }
      waitFor(SLEEP_TIME_FORCE_LOCK_RETRY);
    }
  }

  public SubTaskGroup createManageEncryptionAtRestTask() {
    SubTaskGroup subTaskGroup = null;
    AbstractTaskBase task;
    switch (taskParams().encryptionAtRestConfig.opType) {
      case ENABLE:
        subTaskGroup = createSubTaskGroup("EnableEncryptionAtRest");
        task = createTask(EnableEncryptionAtRest.class);
        EnableEncryptionAtRest.Params enableParams = new EnableEncryptionAtRest.Params();
        enableParams.setUniverseUUID(taskParams().getUniverseUUID());
        enableParams.encryptionAtRestConfig = taskParams().encryptionAtRestConfig;
        task.initialize(enableParams);
        subTaskGroup.addSubTask(task);
        getRunnableTask().addSubTaskGroup(subTaskGroup);
        break;
      case DISABLE:
        subTaskGroup = createSubTaskGroup("DisableEncryptionAtRest");
        task = createTask(DisableEncryptionAtRest.class);
        DisableEncryptionAtRest.Params disableParams = new DisableEncryptionAtRest.Params();
        disableParams.setUniverseUUID(taskParams().getUniverseUUID());
        disableParams.encryptionAtRestConfig = taskParams().encryptionAtRestConfig;
        task.initialize(disableParams);
        subTaskGroup.addSubTask(task);
        getRunnableTask().addSubTaskGroup(subTaskGroup);
        break;
      default:
      case UNDEFINED:
        break;
    }
    return subTaskGroup;
  }

  public SubTaskGroup createSetActiveUniverseKeysTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetActiveUniverseKeys");
    SetActiveUniverseKeys task = createTask(SetActiveUniverseKeys.class);
    SetActiveUniverseKeys.Params params = new SetActiveUniverseKeys.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDestroyEncryptionAtRestTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DestroyEncryptionAtRest");
    DestroyEncryptionAtRest task = createTask(DestroyEncryptionAtRest.class);
    DestroyEncryptionAtRest.Params params = new DestroyEncryptionAtRest.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    if (taskParams().getUniverseUUID() != null) {
      MDC.put("universe-id", taskParams().getUniverseUUID().toString());
    }
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  public Universe forceLockUniverseForUpdate(int expectedUniverseVersion) {
    UniverseUpdaterConfig updaterConfig =
        UniverseUpdaterConfig.builder()
            .checkSuccess(true)
            .expectedUniverseVersion(expectedUniverseVersion)
            .forceUpdate(true)
            .build();
    return lockUniverseForUpdate(
        taskParams().getUniverseUUID(), getLockingUniverseUpdater(updaterConfig));
  }

  /**
   * Locks the universe by setting the 'updateInProgress' flag and associating with the task. If the
   * universe is already being modified, it throws an exception. Any tasks involving tables should
   * use this method, not any other because this does not update the 'updateSucceeded' flag.
   *
   * @param expectedUniverseVersion Lock only if the current version of the universe is at this
   *     version. -1 implies always lock the universe.
   * @return the universe.
   */
  public Universe lockUniverse(int expectedUniverseVersion) {
    UniverseUpdaterConfig updaterConfig =
        UniverseUpdaterConfig.builder().expectedUniverseVersion(expectedUniverseVersion).build();
    UniverseUpdater updater = getLockingUniverseUpdater(updaterConfig);
    return lockUniverseForUpdate(taskParams().getUniverseUUID(), updater);
  }

  /** See {@link #lockAndFreezeUniverseForUpdate(UUID, int, Consumer)} */
  public Universe lockAndFreezeUniverseForUpdate(
      int expectedUniverseVersion, @Nullable Consumer<Universe> firstRunTxnCallback) {
    return lockAndFreezeUniverseForUpdate(
        taskParams().getUniverseUUID(), expectedUniverseVersion, firstRunTxnCallback);
  }

  /**
   * This method locks the universe, runs {@link #createPrecheckTasks(Universe)}, and freezes the
   * universe with the given txnCallback. By freezing, the association between the task and the
   * universe is set up such that the universe always has a reference to the task.
   *
   * @param universeUuid the universe UUID.
   * @param expectedUniverseVersion Lock only if the current version of the universe is at this
   *     version. -1 implies always lock the universe.
   * @param firstRunTxnCallback the callback to be invoked in transaction when the universe is
   *     frozen on the first run of the task.
   * @return the universe.
   */
  public Universe lockAndFreezeUniverseForUpdate(
      UUID universeUuid,
      int expectedUniverseVersion,
      @Nullable Consumer<Universe> firstRunTxnCallback) {
    UniverseUpdaterConfig updaterConfig =
        UniverseUpdaterConfig.builder()
            .expectedUniverseVersion(expectedUniverseVersion)
            .freezeUniverse(false)
            .build();
    UniverseUpdater updater = getLockingUniverseUpdater(updaterConfig);
    Universe universe = lockUniverseForUpdate(universeUuid, updater);
    try {
      createPrecheckTasks(universe);
      if (isFirstTry()) {
        createFreezeUniverseTask(universeUuid, firstRunTxnCallback)
            .setSubTaskGroupType(SubTaskGroupType.ValidateConfigurations);
        // Run to apply the change first before adding the rest of the subtasks.
        getRunnableTask().runSubTasks();
      } else {
        createFreezeUniverseTask(universeUuid)
            .setSubTaskGroupType(SubTaskGroupType.ValidateConfigurations);
      }
      return Universe.getOrBadRequest(universeUuid);
    } catch (RuntimeException e) {
      unlockUniverseForUpdate(universeUuid);
      throw e;
    }
  }

  /**
   * Similar to {@link #createFreezeUniverseTask(Consumer)} without the callback.
   *
   * @param universeUuid the universe UUID.
   * @return
   */
  private SubTaskGroup createFreezeUniverseTask(UUID universeUuid) {
    return createFreezeUniverseTask(universeUuid, null);
  }

  /**
   * Creates a subtask to freeze the universe {@link #freezeUniverse(Consumer)}.
   *
   * @param universeUuid the universe UUID.
   * @param callback the callback to be executed in transaction when the universe is frozen.
   * @return the subtask group.
   */
  private SubTaskGroup createFreezeUniverseTask(
      UUID universeUuid, @Nullable Consumer<Universe> callback) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup(
            FreezeUniverse.class.getSimpleName(), SubTaskGroupType.ValidateConfigurations);
    FreezeUniverse task = createTask(FreezeUniverse.class);
    FreezeUniverse.Params params = new FreezeUniverse.Params();
    params.setUniverseUUID(universeUuid);
    params.setCallback(callback);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Similar to {@link #lockUniverse(int)} but it ignores if the universe does not exist. */
  public Universe lockUniverseIfExist(UUID universeUuid, int expectedUniverseVersion) {
    UniverseUpdaterConfig updaterConfig =
        UniverseUpdaterConfig.builder()
            .expectedUniverseVersion(expectedUniverseVersion)
            .ignoreAbsence(true)
            .build();
    return lockUniverseForUpdate(universeUuid, getLockingUniverseUpdater(updaterConfig));
  }

  public Universe unlockUniverseForUpdate(UUID universeUuid) {
    return unlockUniverseForUpdate(universeUuid, null, true);
  }

  public Universe unlockUniverseForUpdate(boolean updateTaskDetails) {
    return unlockUniverseForUpdate(taskParams().getUniverseUUID(), null, updateTaskDetails);
  }

  public Universe unlockUniverseForUpdate(String error) {
    return unlockUniverseForUpdate(taskParams().getUniverseUUID(), error, true);
  }

  public Universe unlockUniverseForUpdate() {
    return unlockUniverseForUpdate(taskParams().getUniverseUUID(), null, true);
  }

  private Universe unlockUniverseForUpdate(
      UUID universeUUID, String error, boolean updateTaskDetails) {
    ExecutionContext executionContext = getOrCreateExecutionContext();
    if (!executionContext.isLocked(universeUUID)) {
      log.warn("Unlock universe({}) called when it was not locked.", universeUUID);
      return null;
    }
    // Create the update lambda.
    UniverseUpdater updater =
        universe -> {
          // If this universe is not being edited, fail the request.
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          if (!universeDetails.updateInProgress) {
            String msg = "Universe " + universeUUID + " is not being edited.";
            log.error(msg);
            throw new RuntimeException(msg);
          }
          // Persist the updated information about the universe. Mark it as being not edited.
          universeDetails.updateInProgress = false;
          universeDetails.setErrorString(error);
          if (universeDetails.updateSucceeded && updateTaskDetails) {
            // Clear the task UUIDs only if the update succeeded.
            universeDetails.updatingTaskUUID = null;
            if (PLACEMENT_MODIFICATION_TASKS.contains(universeDetails.updatingTask)) {
              universeDetails.placementModificationTaskUuid = null;
              // Do not save the transient state in the universe.
              universeDetails.nodeDetailsSet.forEach(n -> n.masterState = null);
            }
          }
          universeDetails.updatingTask = null;
          universe.setUniverseDetails(universeDetails);
        };
    // Update the progress flag to false irrespective of the version increment failure.
    // Universe version in master does not need to be updated as this does not change
    // the Universe state. It simply sets updateInProgress flag to false.
    Universe universe = Universe.saveDetails(universeUUID, updater, false);
    executionContext.unlockUniverse(universeUUID);
    log.info("Unlocked universe {} for updates.", universeUUID);
    return universe;
  }

  public SubTaskGroup getAnsibleConfigureYbcServerTasks(
      AnsibleConfigureServers.Params params, Universe universe) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) on the universe: %s",
            SubTaskGroupType.UpdatingYbcGFlags, universe.getName());
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public AnsibleConfigureServers.Params getBaseAnsibleServerTaskParams(
      UserIntent userIntent,
      NodeDetails node,
      ServerType processType,
      UpgradeTaskParams.UpgradeTaskType type,
      UpgradeTaskParams.UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();

    // Set the device information (numVolumes, volumeSize, etc.)
    params.deviceInfo = userIntent.getDeviceInfoForNode(node);
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // Add in the node placement uuid.
    params.placementUuid = node.placementUuid;
    // Sets the isMaster field
    params.isMaster = node.isMaster;
    params.enableYSQL = userIntent.enableYSQL;
    params.enableYCQL = userIntent.enableYCQL;
    params.enableYCQLAuth = userIntent.enableYCQLAuth;
    params.enableYSQLAuth = userIntent.enableYSQLAuth;
    params.auditLogConfig = userIntent.auditLogConfig;

    // The software package to install for this cluster.
    params.ybSoftwareVersion = userIntent.ybSoftwareVersion;

    params.instanceType = node.cloudInfo.instance_type;
    params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    params.enableYEDIS = userIntent.enableYEDIS;

    params.type = type;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());
    params.ybcGflags = userIntent.ybcFlags;

    if (userIntent.providerType.equals(CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    return params;
  }

  /** Create a task to mark the change on a universe as success. */
  public SubTaskGroup createMarkUniverseUpdateSuccessTasks() {
    return createMarkUniverseUpdateSuccessTasks(taskParams().getUniverseUUID());
  }

  public SubTaskGroup createMarkUniverseUpdateSuccessTasks(UUID universeUuid) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("FinalizeUniverseUpdate");
    UniverseUpdateSucceeded.Params params = new UniverseUpdateSucceeded.Params();
    params.setUniverseUUID(universeUuid);
    UniverseUpdateSucceeded task = createTask(UniverseUpdateSucceeded.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createChangeAdminPasswordTask(
      Cluster primaryCluster,
      String ysqlPassword,
      String ysqlCurrentPassword,
      String ysqlUserName,
      String ysqlDbName,
      String ycqlPassword,
      String ycqlCurrentPassword,
      String ycqlUserName) {
    return createChangeAdminPasswordTask(
        primaryCluster,
        ysqlPassword,
        ysqlCurrentPassword,
        ysqlUserName,
        ysqlDbName,
        ycqlPassword,
        ycqlCurrentPassword,
        ycqlUsername,
        false /* validateCurrentPassword */);
  }

  public SubTaskGroup createChangeAdminPasswordTask(
      Cluster primaryCluster,
      String ysqlPassword,
      String ysqlCurrentPassword,
      String ysqlUserName,
      String ysqlDbName,
      String ycqlPassword,
      String ycqlCurrentPassword,
      String ycqlUserName,
      boolean validateCurrentPassword) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ChangeAdminPassword");
    ChangeAdminPassword.Params params = new ChangeAdminPassword.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.primaryCluster = primaryCluster;
    params.ycqlNewPassword = ycqlPassword;
    params.ysqlNewPassword = ysqlPassword;
    params.ycqlCurrentPassword = ycqlCurrentPassword;
    params.ysqlCurrentPassword = ysqlCurrentPassword;
    params.ycqlUserName = ycqlUserName;
    params.ysqlUserName = ysqlUserName;
    params.ysqlDbName = ysqlDbName;
    params.validateCurrentPassword = validateCurrentPassword;
    ChangeAdminPassword task = createTask(ChangeAdminPassword.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void checkAndCreateChangeAdminPasswordTask(Cluster primaryCluster) {
    boolean changeYCQLAdminPass =
        primaryCluster.userIntent.enableYCQL
            && primaryCluster.userIntent.enableYCQLAuth
            && !primaryCluster.userIntent.defaultYcqlPassword;
    boolean changeYSQLAdminPass =
        primaryCluster.userIntent.enableYSQL
            && primaryCluster.userIntent.enableYSQLAuth
            && !primaryCluster.userIntent.defaultYsqlPassword;
    // Change admin password for Admin user, as specified.
    if (changeYCQLAdminPass || changeYSQLAdminPass) {
      createChangeAdminPasswordTask(
              primaryCluster,
              ysqlPassword,
              ysqlCurrentPassword,
              ysqlUsername,
              ysqlDb,
              ycqlPassword,
              ycqlCurrentPassword,
              ycqlUsername)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  /** Create a task to mark the final software version on a universe. */
  public SubTaskGroup createUpdateSoftwareVersionTask(
      String softwareVersion, boolean isSoftwareUpdateViaVm) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("FinalizeUniverseUpdate");
    UpdateSoftwareVersion.Params params = new UpdateSoftwareVersion.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.softwareVersion = softwareVersion;
    params.prevSoftwareVersion = taskParams().ybPrevSoftwareVersion;
    params.isSoftwareUpdateViaVm = isSoftwareUpdateViaVm;
    UpdateSoftwareVersion task = createTask(UpdateSoftwareVersion.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateYbcTask(String ybcSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("FinalizeYbcUpdate");
    UpdateUniverseYbcDetails.Params params = new UpdateUniverseYbcDetails.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.setYbcSoftwareVersion(ybcSoftwareVersion);
    params.setEnableYbc(true);
    params.setYbcInstalled(true);
    UpdateUniverseYbcDetails task = createTask(UpdateUniverseYbcDetails.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateYbcGFlagInTheUniverseDetailsTask(Map<String, String> ybcGflags) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("FinalizeYbcUpdate");
    UpdateUniverseYbcGflagsDetails.Params params = new UpdateUniverseYbcGflagsDetails.Params();
    params.ybcGflags = ybcGflags;
    UpdateUniverseYbcGflagsDetails task = createTask(UpdateUniverseYbcGflagsDetails.class);
    params.setUniverseUUID(taskParams().getUniverseUUID());
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates task to update disabled ybc state in universe details. */
  public SubTaskGroup createDisableYbcUniverseDetails() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateDisableYbcDetails");
    UpdateUniverseYbcDetails.Params params = new UpdateUniverseYbcDetails.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.setYbcSoftwareVersion(null);
    params.setEnableYbc(false);
    params.setYbcInstalled(false);
    UpdateUniverseYbcDetails task = createTask(UpdateUniverseYbcDetails.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createMarkUniverseForHealthScriptReUploadTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("MarkUniverseForHealthScriptReUpload");
    MarkUniverseForHealthScriptReUpload task =
        createTask(MarkUniverseForHealthScriptReUpload.class);
    task.initialize(taskParams());
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateSoftwareVersionTask(String softwareVersion) {
    return createUpdateSoftwareVersionTask(softwareVersion, false /*isSoftwareUpdateViaVm*/);
  }

  /** Create a task to run YSQL upgrade on the universe. */
  public SubTaskGroup createRunYsqlUpgradeTask(String ybSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RunYsqlUpgrade");

    RunYsqlUpgrade task = createTask(RunYsqlUpgrade.class);

    RunYsqlUpgrade.Params params = new RunYsqlUpgrade.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.ybSoftwareVersion = ybSoftwareVersion;

    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * @deprecated Create a task to promote external class auto flags on a universe.
   * @param universeUUID
   * @param ignoreErrors
   * @return
   */
  @Deprecated
  public SubTaskGroup createPromoteAutoFlagTask(UUID universeUUID, boolean ignoreErrors) {
    return createPromoteAutoFlagTask(
        universeUUID, ignoreErrors, AutoFlagUtil.EXTERNAL_AUTO_FLAG_CLASS_NAME);
  }

  /**
   * Create a task to promote autoflags upto a maxClass on a universe.
   *
   * @param universeUUID
   * @param ignoreErrors
   * @param maxClass
   * @return
   */
  public SubTaskGroup createPromoteAutoFlagTask(
      UUID universeUUID, boolean ignoreErrors, String maxClass) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("PromoteAutoFlag");
    PromoteAutoFlags task = createTask(PromoteAutoFlags.class);
    PromoteAutoFlags.Params params = new PromoteAutoFlags.Params();
    params.ignoreErrors = ignoreErrors;
    params.maxClass = maxClass;
    params.setUniverseUUID(universeUUID);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to promote auto flags to the rollback version on a universe.
   *
   * @param universeUUID
   * @param rollbackVersion
   * @return
   */
  public SubTaskGroup createRollbackAutoFlagTask(UUID universeUUID, int rollbackVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RollbackAutoFlag");
    RollbackAutoFlags task = createTask(RollbackAutoFlags.class);
    RollbackAutoFlags.Params params = new RollbackAutoFlags.Params();
    params.rollbackVersion = rollbackVersion;
    params.setUniverseUUID(universeUUID);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to store auto flags version of current software version.
   *
   * @param universeUUID
   * @return
   */
  public SubTaskGroup createStoreAutoFlagConfigVersionTask(UUID universeUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("StoreAutoFlagConfig");
    StoreAutoFlagConfigVersion task = createTask(StoreAutoFlagConfigVersion.class);
    StoreAutoFlagConfigVersion.Params params = new StoreAutoFlagConfigVersion.Params();
    params.setUniverseUUID(universeUUID);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to check the software version on the universe node. */
  public SubTaskGroup createCheckSoftwareVersionTask(
      Collection<NodeDetails> nodes, String ybSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckSoftwareVersion");
    for (NodeDetails node : nodes) {
      CheckSoftwareVersion task = createTask(CheckSoftwareVersion.class);
      CheckSoftwareVersion.Params params = new CheckSoftwareVersion.Params();
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.nodeName = node.nodeName;
      params.requiredVersion = ybSoftwareVersion;
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to check auto flags before XCluster replication. */
  public SubTaskGroup createCheckXUniverseAutoFlag(
      Universe sourceUniverse, Universe targetUniverse) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckXUniverseAutoFlag");
    CheckXUniverseAutoFlags task = createTask(CheckXUniverseAutoFlags.class);
    CheckXUniverseAutoFlags.Params params = new CheckXUniverseAutoFlags.Params();
    params.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    params.targetUniverseUUID = targetUniverse.getUniverseUUID();
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to check memory limit on the universe nodes. */
  public SubTaskGroup createAvailableMemoryCheck(
      Collection<NodeDetails> nodes, String memoryType, Long memoryLimitKB) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckMemory");
    CheckMemory task = createTask(CheckMemory.class);
    CheckMemory.Params params = new CheckMemory.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.memoryType = memoryType;
    params.memoryLimitKB = memoryLimitKB;
    params.nodeIpList =
        nodes.stream().map(node -> node.cloudInfo.private_ip).collect(Collectors.toList());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to check locale on the universe nodes. */
  public SubTaskGroup createLocaleCheckTask(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckLocale");
    CheckLocale task = createTask(CheckLocale.class);
    CheckLocale.Params params = new CheckLocale.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.nodeNames = nodes.stream().map(node -> node.nodeName).collect(Collectors.toSet());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to check glibc on the universe nodes */
  public SubTaskGroup createCheckGlibcTask(
      Collection<NodeDetails> nodes, String ybSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckGlibc");
    CheckGlibc task = createTask(CheckGlibc.class);
    CheckGlibc.Params params = new CheckGlibc.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.ybSoftwareVersion = ybSoftwareVersion;
    params.nodeNames = nodes.stream().map(node -> node.nodeName).collect(Collectors.toSet());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to preform pre-check for software upgrade. */
  public SubTaskGroup createCheckUpgradeTask(String ybSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckUpgrade");
    CheckUpgrade task = createTask(CheckUpgrade.class);
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.ybSoftwareVersion = ybSoftwareVersion;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to persist changes by ResizeNode task for specific clusters */
  public SubTaskGroup createPersistResizeNodeTask(UserIntent newUserIntent, UUID clusterUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("PersistResizeNode");
    PersistResizeNode.Params params = new PersistResizeNode.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.newUserIntent = newUserIntent;
    params.clusterUUID = clusterUUID;
    PersistResizeNode task = createTask(PersistResizeNode.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to persist changes by Systemd Upgrade task */
  public SubTaskGroup createPersistSystemdUpgradeTask(Boolean useSystemd) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("PersistSystemdUpgrade");
    PersistSystemdUpgrade.Params params = new PersistSystemdUpgrade.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.useSystemd = useSystemd;
    PersistSystemdUpgrade task = createTask(PersistSystemdUpgrade.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to mark the updated cert on a universe. */
  public SubTaskGroup createUnivSetCertTask(UUID certUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("FinalizeUniverseUpdate");
    UnivSetCertificate.Params params = new UnivSetCertificate.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.certUUID = certUUID;
    UnivSetCertificate task = createTask(UnivSetCertificate.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to create default alert definitions on a universe. */
  public SubTaskGroup createUnivCreateAlertDefinitionsTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateAlertDefinitions");
    CreateAlertDefinitions task = createTask(CreateAlertDefinitions.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to activate or diactivate universe alert definitions. */
  public SubTaskGroup createUnivManageAlertDefinitionsTask(boolean active) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ManageAlertDefinitions");
    ManageAlertDefinitions task = createTask(ManageAlertDefinitions.class);
    ManageAlertDefinitions.Params params = new ManageAlertDefinitions.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.active = active;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to ping yb-controller servers on each node */
  public SubTaskGroup createWaitForYbcServerTask(Collection<NodeDetails> nodeDetailsSet) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForYbcServer");
    WaitForYbcServer task = createTask(WaitForYbcServer.class);
    WaitForYbcServer.Params params = new WaitForYbcServer.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.nodeDetailsSet = nodeDetailsSet == null ? null : new HashSet<>(nodeDetailsSet);
    params.nodeNameList =
        nodeDetailsSet == null
            ? null
            : nodeDetailsSet.stream().map(node -> node.nodeName).collect(Collectors.toSet());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to destroy nodes and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be removed
   * @param isForceDelete if this is true, ignore ansible errors
   * @param deleteNode if true, the node info is deleted from the universe db.
   * @param deleteRootVolumes if true, the volumes are deleted.
   * @param skipDestroyPrecheck if true, skips the pre-check validation subtask before destroying
   *     server.
   */
  public SubTaskGroup createDestroyServerTasks(
      Universe universe,
      Collection<NodeDetails> nodes,
      boolean isForceDelete,
      boolean deleteNode,
      boolean deleteRootVolumes,
      boolean skipDestroyPrecheck) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleDestroyServers");
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    nodes = filterUniverseNodes(universe, nodes, n -> true);

    // TODO: Update to use node whitelist when the db implements this.
    if (!skipDestroyPrecheck) {
      createCheckNodeSafeToDeleteTasks(universe, nodes);
    }

    for (NodeDetails node : nodes) {
      // Check if the private ip for the node is set. If not, that means we don't have
      // a clean state to delete the node. Log it, free up the onprem node
      // so that the client can use the node instance to create another universe.
      if (node.cloudInfo.private_ip == null) {
        log.warn(
            String.format(
                "Node %s doesn't have a private IP. Skipping node delete.", node.nodeName));
        if (node.cloudInfo.cloud.equals(
            com.yugabyte.yw.commissioner.Common.CloudType.onprem.name())) {
          try {
            NodeInstance providerNode = NodeInstance.getByName(node.nodeName);
            providerNode.setToFailedCleanup(universe, node);
          } catch (Exception ex) {
            log.warn("On-prem node {} doesn't have a linked instance ", node.nodeName);
          }
          continue;
        }
        if (node.nodeUuid == null) {
          // No other way to identify the node.
          continue;
        }
      }
      Cluster cluster = universe.getCluster(node.placementUuid);
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the node UUID.
      params.nodeUuid = node.nodeUuid;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Flag to be set where errors during Ansible Destroy Server will be ignored.
      params.isForceDelete = isForceDelete;
      // Flag to track if node info should be deleted from universe db.
      params.deleteNode = deleteNode;
      // Flag to track if volumes should be deleted from universe.
      params.deleteRootVolumes = deleteRootVolumes;
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Assign the node IP to ensure deletion of the correct node.
      params.nodeIP = node.cloudInfo.private_ip;
      params.useSystemd = userIntent.useSystemd;
      params.otelCollectorInstalled = universe.getUniverseDetails().otelCollectorEnabled;
      // Create the Ansible task to destroy the server.
      AnsibleDestroyServer task = createTask(AnsibleDestroyServer.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected Collection<NodeDetails> filterNodesForInstallNodeAgent(
      Universe universe, Collection<NodeDetails> nodes) {
    NodeAgentClient nodeAgentClient = application.injector().instanceOf(NodeAgentClient.class);
    Map<UUID, Boolean> clusterSkip = new HashMap<>();
    return nodes.stream()
        .filter(n -> n.cloudInfo != null)
        .filter(
            n ->
                clusterSkip.computeIfAbsent(
                    n.placementUuid,
                    k -> {
                      Cluster cluster = universe.getCluster(n.placementUuid);
                      Provider provider =
                          Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
                      if (!nodeAgentClient.isClientEnabled(provider)) {
                        return false;
                      }
                      if (provider.getCloudCode() == CloudType.onprem) {
                        AccessKey accessKey =
                            AccessKey.getOrBadRequest(
                                provider.getUuid(), cluster.userIntent.accessKeyCode);
                        return !accessKey.getKeyInfo().skipProvisioning;
                      } else if (provider.getCloudCode() != CloudType.aws
                          && provider.getCloudCode() != CloudType.azu
                          && provider.getCloudCode() != CloudType.gcp) {
                        return false;
                      }
                      return true;
                    }))
        .collect(Collectors.toSet());
  }

  /**
   * Filter out nodes that are not in the given universe and do not satisfy the predicate.
   *
   * @param universe the universe against which the nodes are checked.
   * @param nodes the given nodes.
   * @param predicate the predicate on the universe node.
   * @return set of filtered nodes.
   */
  protected Set<NodeDetails> filterUniverseNodes(
      Universe universe, Collection<NodeDetails> nodes, Predicate<NodeDetails> predicate) {
    if (universe != null) {
      // Node name can be null if the submission of the tasks itself fails.
      // Any subsequent task like destroy which calls this method will fail.
      Map<String, NodeDetails> universeNodeDetailsMap =
          universe.getNodes().stream()
              .filter(n -> StringUtils.isNotBlank(n.getNodeName()))
              .collect(Collectors.toMap(NodeDetails::getNodeName, Function.identity()));
      return nodes.stream()
          .map(n -> universeNodeDetailsMap.get(n.getNodeName()))
          .filter(Objects::nonNull)
          .filter(n -> predicate == null || predicate.test(n))
          .collect(Collectors.toSet());
    }
    return Collections.emptySet();
  }

  public SubTaskGroup createInstallNodeAgentTasks(Collection<NodeDetails> nodes) {
    return createInstallNodeAgentTasks(nodes, false);
  }

  public SubTaskGroup createInstallNodeAgentTasks(
      Collection<NodeDetails> nodes, boolean reinstall) {
    Map<UUID, Provider> nodeUuidProviderMap = new HashMap<>();
    SubTaskGroup subTaskGroup = createSubTaskGroup(InstallNodeAgent.class.getSimpleName());
    String installPath = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentInstallPath);
    if (!new File(installPath).isAbsolute()) {
      String errMsg = String.format("Node agent installation path %s is invalid", installPath);
      log.error(errMsg);
      throw new IllegalArgumentException(errMsg);
    }
    int serverPort = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentServerPort);
    Universe universe = getUniverse();
    Customer customer = Customer.get(universe.getCustomerId());
    filterNodesForInstallNodeAgent(universe, nodes)
        .forEach(
            n -> {
              InstallNodeAgent.Params params = new InstallNodeAgent.Params();
              Provider provider =
                  nodeUuidProviderMap.computeIfAbsent(
                      n.placementUuid,
                      k -> {
                        Cluster cluster = universe.getCluster(n.placementUuid);
                        return Provider.getOrBadRequest(
                            UUID.fromString(cluster.userIntent.provider));
                      });
              ProviderDetails providerDetails = provider.getDetails();
              CloudType cloudType = universe.getNodeDeploymentMode(n);
              params.sshUser =
                  StringUtils.isNotBlank(providerDetails.sshUser)
                      ? providerDetails.sshUser
                      : cloudType.getSshUser();
              UniverseDefinitionTaskParams.Cluster cluster =
                  universe.getUniverseDetails().getClusterByUuid(n.placementUuid);
              UUID imageBundleUUID =
                  Util.retreiveImageBundleUUID(
                      universe.getUniverseDetails().arch, cluster.userIntent, provider);
              if (imageBundleUUID != null) {
                ImageBundle.NodeProperties toOverwriteNodeProperties =
                    imageBundleUtil.getNodePropertiesOrFail(
                        imageBundleUUID,
                        n.cloudInfo.region,
                        cluster.userIntent.providerType.toString());
                params.sshUser = toOverwriteNodeProperties.getSshUser();
              }

              params.airgap = provider.getAirGapInstall();
              params.nodeName = n.nodeName;
              params.customerUuid = customer.getUuid();
              params.azUuid = n.azUuid;
              params.setUniverseUUID(universe.getUniverseUUID());
              params.nodeAgentInstallDir = installPath;
              params.nodeAgentPort = serverPort;
              params.reinstall = reinstall;
              if (StringUtils.isNotEmpty(n.sshUserOverride)) {
                params.sshUser = n.sshUserOverride;
              }
              InstallNodeAgent task = createTask(InstallNodeAgent.class);
              task.initialize(params);
              subTaskGroup.addSubTask(task);
            });
    if (subTaskGroup.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    return subTaskGroup;
  }

  protected void deleteNodeAgent(NodeDetails nodeDetails) {
    if (nodeDetails.cloudInfo != null && nodeDetails.cloudInfo.private_ip != null) {
      NodeAgentManager nodeAgentManager = application.injector().instanceOf(NodeAgentManager.class);
      Cluster cluster = getUniverse().getCluster(nodeDetails.placementUuid);
      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      if (provider.getCloudCode() == CloudType.onprem) {
        AccessKey accessKey =
            AccessKey.getOrBadRequest(provider.getUuid(), cluster.userIntent.accessKeyCode);
        if (accessKey.getKeyInfo().skipProvisioning) {
          return;
        }
      }
      NodeAgent.maybeGetByIp(nodeDetails.cloudInfo.private_ip)
          .ifPresent(n -> nodeAgentManager.purge(n));
    }
  }

  public SubTaskGroup createWaitForNodeAgentTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup(WaitForNodeAgent.class.getSimpleName());
    NodeAgentClient nodeAgentClient = application.injector().instanceOf(NodeAgentClient.class);
    for (NodeDetails node : nodes) {
      if (node.cloudInfo == null) {
        continue;
      }
      Cluster cluster = getUniverse().getCluster(node.placementUuid);
      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      if (nodeAgentClient.isClientEnabled(provider)) {
        WaitForNodeAgent.Params params = new WaitForNodeAgent.Params();
        params.nodeName = node.nodeName;
        params.azUuid = node.azUuid;
        params.setUniverseUUID(taskParams().getUniverseUUID());
        params.timeout = Duration.ofMinutes(2);
        WaitForNodeAgent task = createTask(WaitForNodeAgent.class);
        task.initialize(params);
        subTaskGroup.addSubTask(task);
      }
    }
    if (subTaskGroup.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    return subTaskGroup;
  }

  /**
   * Creates a task to delete unused root volumes matching the tags for both the nodes and the
   * universe. If volumeIds is not set or empty, all the matching volumes are deleted, else only the
   * specified matching volumes are deleted.
   *
   * @param universe the universe to which the nodes belong.
   * @param nodes the nodes to which the volumes were attached before.
   * @param volumeIds the volume IDs.
   * @return SubTaskGroup.
   */
  public SubTaskGroup createDeleteRootVolumesTasks(
      Universe universe, Collection<NodeDetails> nodes, Set<String> volumeIds) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteRootVolumes");
    for (NodeDetails node : nodes) {
      if (node.cloudInfo == null || CloudType.onprem.name().equals(node.cloudInfo.cloud)) {
        continue;
      }
      Cluster cluster = universe.getCluster(node.placementUuid);
      DeleteRootVolumes.Params params = new DeleteRootVolumes.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
      params.azUuid = node.azUuid;
      params.nodeName = node.nodeName;
      params.nodeUuid = node.nodeUuid;
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.volumeIds = volumeIds;
      params.isForceDelete = true;
      DeleteRootVolumes task = createTask(DeleteRootVolumes.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to pause the nodes and adds to the task queue.
   *
   * @param nodes : a collection of nodes that need to be paused.
   */
  public SubTaskGroup createPauseServerTasks(Universe universe, Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("PauseServer");
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    for (NodeDetails node : nodes) {
      // Check if the private ip for the node is set. If not, that means we don't have
      // a clean state to pause the node. Log it and skip the node.
      if (node.cloudInfo.private_ip == null) {
        log.warn(
            String.format("Node %s doesn't have a private IP. Skipping pause.", node.nodeName));
        continue;
      }
      PauseServer.Params params = new PauseServer.Params();
      Cluster cluster = universe.getCluster(node.placementUuid);
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Assign the node IP to pause the node.
      params.nodeIP = node.cloudInfo.private_ip;
      params.useSystemd = userIntent.useSystemd;
      // Create the task to pause the server.
      PauseServer task = createTask(PauseServer.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task list to resume nodes in the universe and adds it to the task queue. */
  public SubTaskGroup createResumeServerTasks(Universe universe) {
    Collection<NodeDetails> nodes = universe.getNodes();
    SubTaskGroup subTaskGroup = createSubTaskGroup("ResumeServer");
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    for (NodeDetails node : nodes) {
      // Check if the private ip for the node is set. If not, that means we don't have
      // a clean state to resume the node. Log it and skip the node.
      if (node.cloudInfo.private_ip == null) {
        log.warn(
            String.format(
                "Node %s doesn't have a private IP. Skipping node resume.", node.nodeName));
        continue;
      }
      Cluster cluster = universe.getCluster(node.placementUuid);
      ResumeServer.Params params = new ResumeServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Assign the node IP to resume the nodes.
      params.nodeIP = node.cloudInfo.private_ip;
      params.useSystemd = userIntent.useSystemd;
      // Create the task to resume the server.
      ResumeServer task = createTask(ResumeServer.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create tasks to update the state of the nodes.
   *
   * @param nodes set of nodes to be updated.
   * @param nodeState State into which these nodes will be transitioned.
   */
  public SubTaskGroup createSetNodeStateTasks(
      Collection<NodeDetails> nodes, NodeDetails.NodeState nodeState) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetNodeState");
    for (NodeDetails node : nodes) {
      SetNodeState.Params params = new SetNodeState.Params();
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.azUuid = node.azUuid;
      params.nodeName = node.nodeName;
      params.state = nodeState;
      SetNodeState task = createTask(SetNodeState.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForKeyInMemoryTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForEncryptionKeyInMemory");
    WaitForEncryptionKeyInMemory.Params params = new WaitForEncryptionKeyInMemory.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.nodeAddress = HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort);
    params.nodeName = node.nodeName;
    WaitForEncryptionKeyInMemory task = createTask(WaitForEncryptionKeyInMemory.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create task to execute a Cluster CTL command against specific process
   *
   * @param node node for which the CTL command needs to be executed
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @return SubTaskGroup
   */
  public SubTaskGroup createServerControlTask(
      NodeDetails node,
      ServerType processType,
      String command,
      Consumer<AnsibleClusterServerCtl.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    subTaskGroup.addSubTask(getServerControlTask(node, processType, command, 0, paramsCustomizer));
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createServerControlTask(
      NodeDetails node, ServerType processType, String command) {
    return createServerControlTask(node, processType, command, params -> {});
  }

  /**
   * Create task to check if a specific process is ready to serve requests on a given node.
   *
   * @param node node for which the check needs to be executed.
   * @param serverType server process type on the node to the check.
   * @return SubTaskGroup
   */
  public SubTaskGroup createWaitForServerReady(NodeDetails node, ServerType serverType) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForServerReady");
    WaitForServerReady.Params params = new WaitForServerReady.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.nodeName = node.nodeName;
    params.serverType = serverType;
    params.waitTimeMs = getOrCreateExecutionContext().getWaitForServerReadyTimeout().toMillis();
    WaitForServerReady task = createTask(WaitForServerReady.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createCheckFollowerLagTask(NodeDetails node, ServerType serverType) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckFollowerLag");
    ServerSubTaskParams params = new ServerSubTaskParams();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.serverType = serverType;
    params.nodeName = node.nodeName;
    CheckFollowerLag task = createTask(CheckFollowerLag.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /*
   * Create subtask to determine that the node is not part of the universe quorum.
   * Checks that no tablets exists on the tserver (if applicable) and node ip is not
   * part of the quorum.
   *
   * @param universe universe for which the node belongs.
   * @param node node we want to check.
   */
  public void createCheckNodeSafeToDeleteTasks(Universe universe, Collection<NodeDetails> nodes) {
    boolean clusterMembershipCheckEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.clusterMembershipCheckEnabled);
    if (clusterMembershipCheckEnabled) {
      SubTaskGroup subTaskGroup = createSubTaskGroup("CheckNodeSafeToDelete");
      for (NodeDetails node : nodes) {
        NodeTaskParams params = new NodeTaskParams();
        params.setUniverseUUID(taskParams().getUniverseUUID());
        params.nodeName = node.getNodeName();
        CheckNodeSafeToDelete task = createTask(CheckNodeSafeToDelete.class);
        task.initialize(params);
        subTaskGroup.addSubTask(task);
      }
      getRunnableTask().addSubTaskGroup(subTaskGroup);
      subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ValidatingNode);
    }
  }

  /*
   * For a given node, finds the tablets assigned to its tserver (if relevant).
   *
   * @param universe universe for which the node belongs.
   * @param currentNode node we want to check.
   * @return a set of tablets for the associated tserver.
   */
  public Set<String> getTserverTablets(Universe universe, NodeDetails currentNode) {
    // Wait for a maximum of 10 seconds for url to succeed.
    NodeDetails masterLeaderNode = universe.getMasterLeaderNode();
    HostAndPort masterLeaderHostPort =
        HostAndPort.fromParts(
            masterLeaderNode.cloudInfo.private_ip, masterLeaderNode.masterHttpPort);
    String masterLeaderUrl =
        String.format("http://%s%s", masterLeaderHostPort.toString(), DUMP_ENTITIES_URL_SUFFIX);

    RetryTaskUntilCondition<DumpEntitiesResponse> waitForCheck =
        new RetryTaskUntilCondition<>(
            () -> {
              log.debug("Making url request to endpoint: {}", masterLeaderUrl);
              JsonNode masterLeaderDumpJson = nodeUIApiHelper.getRequest(masterLeaderUrl);
              DumpEntitiesResponse dumpEntities =
                  Json.fromJson(masterLeaderDumpJson, DumpEntitiesResponse.class);
              return dumpEntities;
            },
            (d) -> {
              if (d.getError() != null) {
                log.warn("Url request to {} failed with error {}", masterLeaderUrl, d.getError());
                return false;
              }
              return true;
            });

    DumpEntitiesResponse dumpEntitiesResponse = waitForCheck.retryWithBackoff(1, 2, 10);

    HostAndPort currentNodeHP =
        HostAndPort.fromParts(currentNode.cloudInfo.private_ip, currentNode.tserverRpcPort);

    return dumpEntitiesResponse.getTabletsByTserverAddress(currentNodeHP);
  }

  /*
   * Checks whether or not the node has a master process in the universe quorum
   *
   * @param universe universe for which the node belongs
   * @param currentNode node we want to check for
   * @return whether or not the node has a master process in the universe in the quorum
   */
  protected boolean nodeInMasterConfig(Universe universe, NodeDetails node) {
    String ip = node.cloudInfo.private_ip;
    String secondary_ip = node.cloudInfo.secondary_private_ip;
    String masterAddresses = universe.getMasterAddresses();

    try (YBClient client =
        ybService.getClient(masterAddresses, universe.getCertificateNodetoNode())) {
      ListMastersResponse response = client.listMasters();
      List<ServerInfo> servers = response.getMasters();
      return servers.stream()
          .anyMatch(s -> s.getHost().equals(ip) || s.getHost().equals(secondary_ip));
    } catch (Exception e) {
      String msg =
          String.format(
              "Error when fetching listMasters rpc for node %s - %s",
              node.nodeName, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }

  /**
   * Gets the current masters reported by the DB for the given universe.
   *
   * @param universe the given universe.
   * @return Set of master nodes.
   */
  protected Set<NodeDetails> getRemoteMasterNodes(Universe universe) {
    String masterAddresses = universe.getMasterAddresses();
    try (YBClient client =
        ybService.getClient(masterAddresses, universe.getCertificateNodetoNode())) {
      return client.listMasters().getMasters().stream()
          .map(
              serverInfo -> {
                // Port in ServerInfo is set to 0.
                NodeDetails node = universe.getNodeByAnyIP(serverInfo.getHost());
                if (node == null || !node.isMaster) {
                  String errMsg =
                      String.format(
                          "Master %s on DB is not in YBA masters %s",
                          serverInfo.getHost(), masterAddresses);
                  log.error(errMsg);
                  throw new IllegalStateException(errMsg);
                }
                return node;
              })
          .collect(Collectors.toSet());
    } catch (Exception e) {
      String msg =
          String.format(
              "Error while getting masters from DB. Current YBA masters %s - %s",
              masterAddresses, e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg);
    }
  }

  /**
   * Create tasks to execute Cluster CTL command against specific process in parallel
   *
   * @param nodes set of nodes to issue control command in parallel.
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @return SubTaskGroup
   */
  public SubTaskGroup createServerControlTasks(
      List<NodeDetails> nodes,
      ServerType processType,
      String command,
      Consumer<AnsibleClusterServerCtl.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getServerControlTask(node, processType, command, 0, paramsCustomizer));
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createServerControlTasks(
      List<NodeDetails> nodes, ServerType processType, String command) {
    return createServerControlTasks(nodes, processType, command, params -> {});
  }

  private AnsibleClusterServerCtl getServerControlTask(
      NodeDetails node,
      ServerType processType,
      String command,
      int sleepAfterCmdMillis,
      Consumer<AnsibleClusterServerCtl.Params> paramsCustomizer) {
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    UserIntent userIntent = getUserIntent();
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // The service and the command we want to run.
    params.process = processType.toString().toLowerCase();
    params.command = command;
    params.sleepAfterCmdMills = sleepAfterCmdMillis;

    params.setEnableYbc(taskParams().isEnableYbc());
    params.setYbcSoftwareVersion(taskParams().getYbcSoftwareVersion());
    params.installYbc = taskParams().installYbc;
    params.setYbcInstalled(taskParams().isYbcInstalled());
    // sshPortOverride, in case the passed imageBundle has a different port
    // configured for the region.
    params.sshPortOverride = node.sshPortOverride;

    // Set the InstanceType
    params.instanceType = node.cloudInfo.instance_type;
    params.checkVolumesAttached = processType == ServerType.TSERVER && command.equals("start");
    params.useSystemd = userIntent.useSystemd;
    paramsCustomizer.accept(params);
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
    task.initialize(params);
    return task;
  }

  /**
   * Create task to update the state of single node.
   *
   * @param node node for which we need to update the state
   * @param nodeState State into which these nodes will be transitioned.
   */
  public SubTaskGroup createSetNodeStateTask(NodeDetails node, NodeDetails.NodeState nodeState) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetNodeState");
    SetNodeState.Params params = new SetNodeState.Params();
    params.azUuid = node.azUuid;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.nodeName = node.nodeName;
    params.state = nodeState;
    SetNodeState task = createTask(SetNodeState.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create tasks to update the status of the nodes.
   *
   * @param nodes the set if nodes to be updated.
   * @param nodeStatus the status into which these nodes will be transitioned.
   */
  public SubTaskGroup createSetNodeStatusTasks(
      Collection<NodeDetails> nodes, NodeStatus nodeStatus) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetNodeStatus");
    for (NodeDetails node : nodes) {
      SetNodeStatus.Params params = new SetNodeStatus.Params();
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.azUuid = node.azUuid;
      params.nodeName = node.nodeName;
      params.nodeStatus = nodeStatus;
      SetNodeStatus task = createTask(SetNodeStatus.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to update the swamper target file
   *
   * @param removeFile, flag to state if we want to remove the swamper or not
   */
  public void createSwamperTargetUpdateTask(boolean removeFile) {
    if (!config.getBoolean(MetricQueryHelper.PROMETHEUS_MANAGEMENT_ENABLED)) {
      return;
    }
    SubTaskGroup subTaskGroup = createSubTaskGroup("SwamperTargetFileUpdate");
    SwamperTargetsFileUpdate.Params params = new SwamperTargetsFileUpdate.Params();
    SwamperTargetsFileUpdate task = createTask(SwamperTargetsFileUpdate.class);
    params.universeUUID = taskParams().getUniverseUUID();
    params.removeFile = removeFile;
    task.initialize(params);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  /**
   * Create a task to create a table.
   *
   * @param tableType type of the table.
   * @param tableName name of the table.
   * @param tableDetails table options and related details.
   * @param ifNotExist create only if it does not exist.
   */
  public SubTaskGroup createTableTask(
      TableType tableType, String tableName, TableDetails tableDetails, boolean ifNotExist) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateTable");
    CreateTable task = createTask(CreateTable.class);
    CreateTable.Params params = new CreateTable.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.tableType = tableType;
    params.tableName = tableName;
    params.tableDetails = tableDetails;
    params.ifNotExist = ifNotExist;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void checkAndCreateRedisTableTask(Cluster primaryCluster) {
    if (primaryCluster.userIntent.enableYEDIS) {
      // Create a simple redis table.
      createTableTask(
              TableType.REDIS_TABLE_TYPE,
              YBClient.REDIS_DEFAULT_TABLE_NAME,
              null /* table details */,
              true /* ifNotExist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  /** Create a task to create write/read test table wor write/read metric and alert. */
  public SubTaskGroup createReadWriteTestTableTask(int numPartitions, boolean ifNotExist) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateReadWriteTestTable");

    CreateTable task = createTask(CreateTable.class);

    ColumnDetails idColumn = new ColumnDetails();
    idColumn.isClusteringKey = true;
    idColumn.name = "id";
    idColumn.type = YQLDataType.SMALLINT;
    idColumn.sortOrder = SortOrder.ASC;

    TableDetails details = new TableDetails();
    details.tableName = "write_read_test";
    details.keyspace = SYSTEM_PLATFORM_DB;
    details.columns = new ArrayList<>();
    details.columns.add(idColumn);
    // Split at 0, 100, 200, 300 ... (numPartitions - 1) * 100
    if (numPartitions > 1) {
      details.splitValues =
          IntStream.range(0, numPartitions)
              .mapToObj(num -> String.valueOf(num * 100))
              .collect(Collectors.toList());
    }

    CreateTable.Params params = new CreateTable.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.tableType = TableType.PGSQL_TABLE_TYPE;
    params.tableName = details.tableName;
    params.tableDetails = details;
    params.ifNotExist = ifNotExist;

    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void checkAndCreateReadWriteTestTableTask(Cluster primaryCluster) {
    boolean isWriteReadTableRelease =
        CommonUtils.isReleaseEqualOrAfter(
            MIN_WRITE_READ_TABLE_CREATION_RELEASE, primaryCluster.userIntent.ybSoftwareVersion);
    boolean isWriteReadTableEnabled =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.dbReadWriteTest);
    if (primaryCluster.userIntent.enableYSQL
        && isWriteReadTableRelease
        && isWriteReadTableEnabled) {
      // Create read-write test table
      List<NodeDetails> tserverLiveNodes =
          getUniverse().getUniverseDetails().getNodesInCluster(primaryCluster.uuid).stream()
              .filter(nodeDetails -> nodeDetails.isTserver)
              .collect(Collectors.toList());
      createReadWriteTestTableTask(tserverLiveNodes.size(), true)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  /**
   * Create a task to delete a table.
   *
   * @param params The necessary parameters for dropping a table.
   */
  public SubTaskGroup createDeleteTableFromUniverseTask(DeleteTableFromUniverse.Params params) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteTableFromUniverse");
    DeleteTableFromUniverse task = createTask(DeleteTableFromUniverse.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It creates a subtask to delete a set of tables in one universe.
   *
   * <p>Note: DeleteTablesFromUniverse deletes the tables in sequence because the coreDB might not
   * support deleting several tables in parallel.
   *
   * @param universeUuid The UUID of the universe to delete the tables from
   * @param keyspaceTablesMap A map from keyspace name to a list of tables' names in that keyspace
   *     to be deleted
   */
  public SubTaskGroup createDeleteTablesFromUniverseTask(
      UUID universeUuid, Map<String, List<String>> keyspaceTablesMap) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteTablesFromUniverse");
    DeleteTablesFromUniverse.Params deleteTablesFromUniverseParams =
        new DeleteTablesFromUniverse.Params();
    deleteTablesFromUniverseParams.setUniverseUUID(universeUuid);
    deleteTablesFromUniverseParams.keyspaceTablesMap = keyspaceTablesMap;

    DeleteTablesFromUniverse task = createTask(DeleteTablesFromUniverse.class);
    task.initialize(deleteTablesFromUniverseParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a subtask to delete a database/keyspace if it exists.
   *
   * @param keyspaceName : name of the database/keyspace to delete.
   * @param tableType : Type of the Table YSQL/ YCQL
   */
  public SubTaskGroup createDeleteKeySpaceTask(
      String keyspaceName, TableType tableType, boolean ysqlForce) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteKeyspace");
    // Create required params for this subtask.
    DeleteKeyspace.Params params = new DeleteKeyspace.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.setKeyspace(keyspaceName);
    params.backupType = tableType;
    params.ysqlForce = ysqlForce;
    // Create the task.
    DeleteKeyspace task = createTask(DeleteKeyspace.class);
    // Initialize the task.
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForServersTasks(Collection<NodeDetails> nodes, ServerType type) {
    return createWaitForServersTasks(
        nodes,
        type,
        config.getDuration("yb.wait_for_server_timeout") /* default timeout */,
        null /* userIntent */,
        null /* communicationPorts */);
  }

  public SubTaskGroup createWaitForServersTasks(
      Collection<NodeDetails> nodes,
      ServerType type,
      UserIntent userIntent,
      CommunicationPorts communicationPorts) {
    return createWaitForServersTasks(
        nodes,
        type,
        config.getDuration("yb.wait_for_server_timeout") /* default timeout */,
        userIntent,
        communicationPorts);
  }

  public SubTaskGroup createWaitForServersTasks(
      Collection<NodeDetails> nodes, ServerType type, Duration timeout) {
    return createWaitForServersTasks(
        nodes, type, timeout, null /* userIntent */, null /* communicationPorts */);
  }

  /**
   * Create a task list to ping all servers until they are up.
   *
   * @param nodes : a collection of nodes that need to be pinged.
   * @param type : Master or tserver type server running on this node.
   * @param timeout : time to wait for each rpc call to the server.
   * @param userIntent : userIntent of the node.
   * @param communicationPorts: custom communication ports of the node.
   */
  public SubTaskGroup createWaitForServersTasks(
      Collection<NodeDetails> nodes,
      ServerType type,
      Duration timeout,
      @Nullable UserIntent userIntent,
      @Nullable UniverseTaskParams.CommunicationPorts communicationPorts) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForServer");
    for (NodeDetails node : nodes) {
      WaitForServer.Params params = new WaitForServer.Params();
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.nodeName = node.nodeName;
      params.serverType = type;
      params.serverWaitTimeoutMs = timeout.toMillis();
      params.userIntent = userIntent;
      params.customCommunicationPorts = communicationPorts;
      WaitForServer task = createTask(WaitForServer.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createUpdateMountedDisksTask(
      NodeDetails node, String currentInstanceType, DeviceInfo currentDeviceInfo) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateMountedDisks");
    UpdateMountedDisks.Params params = new UpdateMountedDisks.Params();

    params.nodeName = node.nodeName;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azUuid = node.azUuid;
    params.instanceType = currentInstanceType;
    params.deviceInfo = currentDeviceInfo;

    UpdateMountedDisks updateMountedDisksTask = createTask(UpdateMountedDisks.class);
    updateMountedDisksTask.initialize(params);
    subTaskGroup.addSubTask(updateMountedDisksTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup updateGFlagsPersistTasks(
      Map<String, String> masterGFlags, Map<String, String> tserverGFlags) {
    return updateGFlagsPersistTasks(null, masterGFlags, tserverGFlags, null);
  }

  /** Creates a task to persist customized gflags to be used by server processes. */
  public SubTaskGroup updateGFlagsPersistTasks(
      @Nullable Cluster cluster,
      Map<String, String> masterGFlags,
      Map<String, String> tserverGFlags,
      @Nullable SpecificGFlags specificGFlags) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateAndPersistGFlags");
    UpdateAndPersistGFlags.Params params = new UpdateAndPersistGFlags.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    params.specificGFlags = specificGFlags;
    if (cluster != null) {
      params.clusterUUIDs = Collections.singletonList(cluster.uuid);
    }
    UpdateAndPersistGFlags task = createTask(UpdateAndPersistGFlags.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to bulk import data from an s3 bucket into a given table.
   *
   * @param taskParams Info about the table and universe of the table to import into.
   */
  public SubTaskGroup createBulkImportTask(BulkImportParams taskParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BulkImport");
    BulkImport task = createTask(BulkImport.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task delete the given node name from the univers.
   *
   * @param nodeName name of a node in the taskparams' uuid universe.
   */
  public SubTaskGroup createDeleteNodeFromUniverseTask(String nodeName) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteNode");
    NodeTaskParams params = new NodeTaskParams();
    params.nodeName = nodeName;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    DeleteNode task = createTask(DeleteNode.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Add or Remove Master process on the node.
   *
   * @param node the node to add/remove master process on.
   * @param isAdd whether Master is being added or removed.
   * @param subTask subtask type
   */
  public void createChangeConfigTasks(
      NodeDetails node, boolean isAdd, UserTaskDetails.SubTaskGroupType subTask) {
    boolean followerLagCheckEnabled =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.followerLagCheckEnabled);
    createChangeConfigTask(node, isAdd, subTask);
    if (isAdd && followerLagCheckEnabled) {
      createCheckFollowerLagTask(node, ServerType.MASTER);
    }
  }

  private void createChangeConfigTask(
      NodeDetails node, boolean isAdd, UserTaskDetails.SubTaskGroupType subTask) {
    // Create a new task list for the change config so that it happens one by one.
    String subtaskGroupName =
        "ChangeMasterConfig(" + node.nodeName + ", " + (isAdd ? "add" : "remove") + ")";
    SubTaskGroup subTaskGroup = createSubTaskGroup(subtaskGroupName);
    // Create the task params.
    ChangeMasterConfig.Params params = new ChangeMasterConfig.Params();
    // Set the azUUID
    params.azUuid = node.azUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // This is an add master.
    params.opType =
        isAdd ? ChangeMasterConfig.OpType.AddMaster : ChangeMasterConfig.OpType.RemoveMaster;
    // Create the task.
    ChangeMasterConfig changeConfig = createTask(ChangeMasterConfig.class);
    changeConfig.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(changeConfig);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    // Configure the user facing subtask for this task list.
    subTaskGroup.setSubTaskGroupType(subTask);
  }

  public SubTaskGroup createTServerTaskForNode(NodeDetails currentNode, String taskType) {
    return createTServerTaskForNode(currentNode, taskType, false /*isIgnoreError*/);
  }

  /**
   * Start T-Server process on the given node
   *
   * @param currentNode the node to operate upon
   * @param taskType Command start/stop
   * @return Subtask group
   */
  public SubTaskGroup createTServerTaskForNode(
      NodeDetails currentNode, String taskType, boolean isIgnoreError) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    UserIntent userIntent = getUserIntent();
    // Add the node name.
    params.nodeName = currentNode.nodeName;
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // Add the az uuid.
    params.azUuid = currentNode.azUuid;
    // The service and the command we want to run.
    params.process = "tserver";
    params.command = taskType;
    params.isIgnoreError = isIgnoreError;
    // Set the InstanceType
    params.instanceType = currentNode.cloudInfo.instance_type;
    params.useSystemd = userIntent.useSystemd;
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Wait for Master Leader Election
   *
   * @return subtask group
   */
  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForMasterLeader");
    WaitForMasterLeader task = createTask(WaitForMasterLeader.class);
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Helper function to create a process update object.
  private UpdateNodeProcess getUpdateTaskProcess(
      String nodeName, ServerType processType, Boolean isAdd) {
    // Create the task params.
    UpdateNodeProcess.Params params = new UpdateNodeProcess.Params();
    params.processType = processType;
    params.isAdd = isAdd;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.nodeName = nodeName;
    UpdateNodeProcess updateNodeProcess = createTask(UpdateNodeProcess.class);
    updateNodeProcess.initialize(params);
    return updateNodeProcess;
  }

  /**
   * Update the process state across all the given servers in Yugaware DB.
   *
   * @param servers : Set of nodes whose process state is to be updated.
   * @param processType : process type: master or tserver.
   * @param isAdd : true if the process is being added, false otherwise.
   */
  public SubTaskGroup createUpdateNodeProcessTasks(
      Set<NodeDetails> servers, ServerType processType, Boolean isAdd) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateNodeProcess");
    for (NodeDetails server : servers) {
      UpdateNodeProcess updateNodeProcess =
          getUpdateTaskProcess(server.nodeName, processType, isAdd);
      // Add it to the task list.
      subTaskGroup.addSubTask(updateNodeProcess);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Update the given node's process state in Yugaware DB,
   *
   * @param nodeName : name of the node where the process state is to be updated.
   * @param processType : process type: master or tserver.
   * @param isAdd : boolean signifying if the process is being added or removed.
   * @return The subtask group.
   */
  public SubTaskGroup createUpdateNodeProcessTask(
      String nodeName, ServerType processType, Boolean isAdd) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateNodeProcess");
    UpdateNodeProcess updateNodeProcess = getUpdateTaskProcess(nodeName, processType, isAdd);
    // Add it to the task list.
    subTaskGroup.addSubTask(updateNodeProcess);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the masters and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need masters to be spawned.
   * @return The subtask group.
   */
  public SubTaskGroup createStartMasterTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      UserIntent userIntent = getUserIntent();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "master";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Start universe with systemd
      params.useSystemd = userIntent.useSystemd;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the tservers and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need tservers to be spawned.
   * @return The subtask group.
   */
  public SubTaskGroup createStartTServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      UserIntent userIntent = getUserIntent();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = ServerType.TSERVER.name().toLowerCase();
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Start universe with systemd
      params.useSystemd = userIntent.useSystemd;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to stop the masters of the cluster and adds it to the task queue.
   *
   * @param nodes set of nodes to be stopped as master.
   */
  public SubTaskGroup createStopMasterTasks(Collection<NodeDetails> nodes) {
    return createStopServerTasks(nodes, ServerType.MASTER, false);
  }

  /**
   * Creates a task list to stop the tservers of the cluster and adds it to the task queue.
   *
   * @param nodes set of nodes to be stopped as tserver.
   */
  public SubTaskGroup createStopTServerTasks(Collection<NodeDetails> nodes) {
    return createStopServerTasks(nodes, ServerType.TSERVER, false);
  }

  /**
   * Creates a task list to stop the yb-controller process on cluster's node and adds it to the
   * queue.
   *
   * @param nodes set of nodes on which yb-controller has to be stopped
   */
  public SubTaskGroup createStopYbControllerTasks(
      Collection<NodeDetails> nodes, boolean isIgnoreError) {
    return createStopServerTasks(nodes, ServerType.CONTROLLER, isIgnoreError);
  }

  public SubTaskGroup createStopYbControllerTasks(Collection<NodeDetails> nodes) {
    return createStopYbControllerTasks(nodes, false /*isIgnoreError*/);
  }

  /**
   * Creates a task list to stop the tservers of the cluster and adds it to the task queue.
   *
   * @param nodes set of nodes to be stopped as master
   */
  public SubTaskGroup createStopServerTasks(
      Collection<NodeDetails> nodes, ServerType serverType, boolean isIgnoreError) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      UserIntent userIntent = getUserIntent();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = serverType.name().toLowerCase();
      params.command = "stop";
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.isIgnoreError = isIgnoreError;
      // Set the systemd parameter.
      params.useSystemd = userIntent.useSystemd;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createTableBackupTask(BackupTableParams taskParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BackupTable");
    BackupTable task = createTask(BackupTable.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Todo: This code is mostly copied from the createBackup task. Use these method in createBackup.
  public BackupTableParams getBackupTableParams(
      BackupRequestParams backupRequestParams, Set<String> tablesToBackup) {
    BackupTableParams backupTableParams = new BackupTableParams(backupRequestParams);
    List<BackupTableParams> backupTableParamsList = new ArrayList<>();
    HashMap<String, BackupTableParams> keyspaceMap = new HashMap<>();
    // Todo: add comments. Backup the whole keyspace.
    Universe universe = Universe.getOrBadRequest(backupRequestParams.getUniverseUUID());
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      ListTablesResponse listTablesResponse =
          client.getTablesList(
              null /* nameFilter */, true /* excludeSystemTables */, null /* namespace */);
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList =
          listTablesResponse.getTableInfoList();
      if (!backupTableParams.isFullBackup) {
        for (BackupRequestParams.KeyspaceTable keyspaceTable :
            backupRequestParams.keyspaceTableList) {
          BackupTableParams backupParams =
              new BackupTableParams(backupRequestParams, keyspaceTable.keyspace);
          if (CollectionUtils.isNotEmpty(keyspaceTable.tableUUIDList)) {
            Set<UUID> tableSet = new HashSet<>(keyspaceTable.tableUUIDList);
            for (UUID tableUUID : tableSet) {
              GetTableSchemaResponse tableSchema = null;
              try {
                tableSchema = client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));
              } catch (Exception e) {
                log.warn(
                    "Error fetching table with UUID: "
                        + tableUUID.toString()
                        + ", skipping backup.");
                continue;
              }
              // If table is not REDIS or YCQL, ignore.
              if (tableSchema.getTableType().equals(TableType.PGSQL_TABLE_TYPE)
                  || !tableSchema.getTableType().equals(backupRequestParams.backupType)
                  || tableSchema.getTableType().equals(TableType.TRANSACTION_STATUS_TABLE_TYPE)
                  || !keyspaceTable.keyspace.equals(tableSchema.getNamespace())) {
                log.info(
                    "Skipping backup of table with UUID: "
                        + tableUUID.toString()
                        + " and keyspace: "
                        + keyspaceTable.keyspace);
                continue;
              }
              backupParams.tableNameList.add(tableSchema.getTableName());
              backupParams.tableUUIDList.add(tableUUID);
              log.info(
                  "Queuing backup for table {}:{}",
                  tableSchema.getNamespace(),
                  CommonUtils.logTableName(tableSchema.getTableName()));
              if (tablesToBackup != null) {
                tablesToBackup.add(
                    String.format("%s:%s", tableSchema.getNamespace(), tableSchema.getTableName()));
              }
            }
            if (CollectionUtils.isNotEmpty(backupParams.tableUUIDList)) {
              backupTableParamsList.add(backupParams);
            }
          } else {
            backupParams.allTables = true;
            for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : tableInfoList) {
              TableType tableType = table.getTableType();
              String tableKeySpace = table.getNamespace().getName();
              String tableUUIDString = table.getId().toStringUtf8();
              String tableName = table.getName();
              UUID tableUUID = getUUIDRepresentation(tableUUIDString);
              if (!tableType.equals(backupRequestParams.backupType)
                  || tableType.equals(TableType.TRANSACTION_STATUS_TABLE_TYPE)
                  || table.getRelationType().equals(MasterTypes.RelationType.INDEX_TABLE_RELATION)
                  || !keyspaceTable.keyspace.equals(tableKeySpace)) {
                log.info(
                    "Skipping keyspace/universe backup of table "
                        + tableUUID
                        + ". Expected keyspace is "
                        + keyspaceTable.keyspace
                        + "; actual keyspace is "
                        + tableKeySpace);
                continue;
              }

              if (tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
                if (!keyspaceMap.containsKey(tableKeySpace)) {
                  keyspaceMap.put(tableKeySpace, backupParams);
                  backupTableParamsList.add(backupParams);
                  if (tablesToBackup != null) {
                    tablesToBackup.add(String.format("%s:%s", tableKeySpace, tableName));
                  }
                }
              } else if (tableType.equals(TableType.YQL_TABLE_TYPE)
                  || tableType.equals(TableType.REDIS_TABLE_TYPE)) {
                if (!keyspaceMap.containsKey(tableKeySpace)) {
                  keyspaceMap.put(tableKeySpace, backupParams);
                  backupTableParamsList.add(backupParams);
                }
                BackupTableParams currentBackup = keyspaceMap.get(tableKeySpace);
                currentBackup.tableNameList.add(tableName);
                currentBackup.tableUUIDList.add(tableUUID);
                if (tablesToBackup != null) {
                  tablesToBackup.add(String.format("%s:%s", tableKeySpace, tableName));
                }
              } else {
                log.error(
                    "Unrecognized table type {} for {}:{}",
                    tableType,
                    tableKeySpace,
                    CommonUtils.logTableName(tableName));
              }
              log.info(
                  "Queuing backup for table {}:{}",
                  tableKeySpace,
                  CommonUtils.logTableName(tableName));
            }
          }
        }
      } else {
        for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : tableInfoList) {
          TableType tableType = table.getTableType();
          String tableKeySpace = table.getNamespace().getName();
          String tableUUIDString = table.getId().toStringUtf8();
          String tableName = table.getName();
          UUID tableUUID = getUUIDRepresentation(tableUUIDString);
          if (!tableType.equals(backupRequestParams.backupType)
              || tableType.equals(TableType.TRANSACTION_STATUS_TABLE_TYPE)
              || table.getRelationType().equals(MasterTypes.RelationType.INDEX_TABLE_RELATION)) {
            log.info("Skipping backup of table " + tableUUID);
            continue;
          }
          if (tableType.equals(TableType.PGSQL_TABLE_TYPE)
              && SYSTEM_PLATFORM_DB.equals(tableKeySpace)) {
            log.info("Skipping " + SYSTEM_PLATFORM_DB + " database");
            continue;
          }

          if (tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
            if (!keyspaceMap.containsKey(tableKeySpace)) {
              BackupTableParams backupParams =
                  new BackupTableParams(backupRequestParams, tableKeySpace);
              backupParams.allTables = true;
              keyspaceMap.put(tableKeySpace, backupParams);
              backupTableParamsList.add(backupParams);
              if (tablesToBackup != null) {
                tablesToBackup.add(String.format("%s:%s", tableKeySpace, tableName));
              }
            }
          } else if (tableType.equals(TableType.YQL_TABLE_TYPE)
              || tableType.equals(TableType.REDIS_TABLE_TYPE)) {
            if (!keyspaceMap.containsKey(tableKeySpace)) {
              BackupTableParams backupParams =
                  new BackupTableParams(backupRequestParams, tableKeySpace);
              backupParams.allTables = true;
              keyspaceMap.put(tableKeySpace, backupParams);
              backupTableParamsList.add(backupParams);
            }
            BackupTableParams currentBackup = keyspaceMap.get(tableKeySpace);
            currentBackup.tableNameList.add(tableName);
            currentBackup.tableUUIDList.add(tableUUID);
            if (tablesToBackup != null) {
              tablesToBackup.add(String.format("%s:%s", tableKeySpace, tableName));
            }
          } else {
            log.error(
                "Unrecognized table type {} for {}:{}",
                tableType,
                tableKeySpace,
                CommonUtils.logTableName(tableName));
          }
          log.info(
              "Queuing backup for table {}:{}", tableKeySpace, CommonUtils.logTableName(tableName));
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    if (backupTableParamsList.isEmpty()) {
      throw new RuntimeException("Invalid Keyspaces or no tables to backup");
    }
    if (backupRequestParams.backupType.equals(TableType.YQL_TABLE_TYPE)
        && backupRequestParams.tableByTableBackup) {
      boolean isTableByTableAllowed =
          confGetter.getConfForScope(universe, UniverseConfKeys.allowTableByTableBackupYCQL);
      if (!isTableByTableAllowed) {
        throw new RuntimeException("YCQL Table by table backup not allowed for this universe");
      }
      backupTableParams.tableByTableBackup = true;
      backupTableParams.backupList = convertToPerTableParams(backupTableParamsList);
    } else {
      backupTableParams.backupList = backupTableParamsList;
    }
    return backupTableParams;
  }

  private List<BackupTableParams> convertToPerTableParams(
      List<BackupTableParams> backupTableParamsList) {
    List<BackupTableParams> flatParamsList = new ArrayList<>();
    backupTableParamsList.stream()
        .forEach(
            bP -> {
              Iterator<UUID> tableUUIDIter = bP.tableUUIDList.iterator();
              Iterator<String> tableNameIter = bP.tableNameList.iterator();
              while (tableUUIDIter.hasNext()) {
                BackupTableParams perTableParam =
                    new BackupTableParams(bP, tableUUIDIter.next(), tableNameIter.next());
                perTableParam.tableByTableBackup = true;
                flatParamsList.add(perTableParam);
              }
            });
    return flatParamsList;
  }

  protected void handleFailedBackupAndRestore(
      List<Backup> backupList,
      List<Restore> restoreList,
      boolean isAbort,
      boolean isLoadBalancerAltered) {
    if (!CollectionUtils.isEmpty(backupList)) {
      for (Backup backup : backupList) {
        if (backup != null && !isAbort && backup.getState().equals(BackupState.InProgress)) {
          backup.transitionState(BackupState.Failed);
          backup.setCompletionTime(new Date());
          backup.save();
        }
      }
    }
    if (!CollectionUtils.isEmpty(restoreList)) {
      for (Restore restore : restoreList) {
        if (restore != null && !isAbort && restore.getState().equals(Restore.State.InProgress)) {
          restore.update(restore.getTaskUUID(), Restore.State.Failed);
        }
      }
    }
    if (isLoadBalancerAltered) {
      setTaskQueueAndRun(
          () ->
              createLoadBalancerStateChangeTask(true)
                  .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse));
    }
  }

  protected Backup createAllBackupSubtasks(
      BackupRequestParams backupRequestParams, SubTaskGroupType subTaskGroupType) {
    return createAllBackupSubtasks(backupRequestParams, subTaskGroupType, false /* ybcBackup */);
  }

  protected Backup createAllBackupSubtasks(
      BackupRequestParams backupRequestParams,
      SubTaskGroupType subTaskGroupType,
      boolean ybcBackup) {
    return createAllBackupSubtasks(
        backupRequestParams, subTaskGroupType, ybcBackup, null /* tablesToBackup */);
  }

  protected Backup createAllBackupSubtasks(
      BackupRequestParams backupRequestParams,
      SubTaskGroupType subTaskGroupType,
      boolean ybcBackup,
      @Nullable Set<String> tablesToBackup) {
    ObjectMapper mapper = new ObjectMapper();
    Universe universe = Universe.getOrBadRequest(backupRequestParams.getUniverseUUID());
    backupHelper.validateBackupRequest(
        backupRequestParams.keyspaceTableList, universe, backupRequestParams.backupType);
    BackupTableParams backupTableParams = getBackupTableParams(backupRequestParams, tablesToBackup);
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;

    createPreflightValidateBackupTask(backupTableParams, ybcBackup)
        .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

    if (!ybcBackup) {
      if (cloudType != CloudType.kubernetes) {
        // Ansible Configure Task for copying xxhsum binaries from
        // third_party directory to the DB nodes.
        installThirdPartyPackagesTask(universe)
            .setSubTaskGroupType(SubTaskGroupType.InstallingThirdPartySoftware);
      } else {
        installThirdPartyPackagesTaskK8s(
                universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.XXHSUM)
            .setSubTaskGroupType(SubTaskGroupType.InstallingThirdPartySoftware);
      }
    }

    if (backupRequestParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subTaskGroupType);
    }

    Backup backup = null;
    if (backupRequestParams.backupUUID != null) {
      backup =
          Backup.getOrBadRequest(backupRequestParams.customerUUID, backupRequestParams.backupUUID);
      backup.transitionState(Backup.BackupState.InProgress);
    } else {
      backup =
          Backup.create(
              backupRequestParams.customerUUID,
              backupTableParams,
              ybcBackup
                  ? Backup.BackupCategory.YB_CONTROLLER
                  : Backup.BackupCategory.YB_BACKUP_SCRIPT,
              Backup.BackupVersion.V2);
      backupRequestParams.backupUUID = backup.getBackupUUID();
      if (ybcBackup) {
        backup.getBackupInfo().initializeBackupDBStates();
      }

      // Save backupUUID to taskInfo of the CreateBackup task.
      try {
        TaskInfo taskInfo = TaskInfo.getOrBadRequest(getUserTaskUUID());
        taskInfo.setDetails(mapper.valueToTree(backupRequestParams));
        taskInfo.save();
      } catch (Exception ex) {
        log.error(ex.getMessage());
      }
    }
    backup.setTaskUUID(getUserTaskUUID());
    backup.save();
    backupTableParams = backup.getBackupInfo();
    backupTableParams.backupUuid = backup.getBackupUUID();
    backupTableParams.baseBackupUUID = backup.getBaseBackupUUID();
    if (ybcBackup) {
      createTableBackupTasksYbc(backupTableParams, backupRequestParams.parallelDBBackups)
          .setSubTaskGroupType(subTaskGroupType);
    } else {
      // Creating encrypted universe key file only needed for non-ybc backups.
      backupTableParams.backupList.stream()
          .forEach(
              paramEntry ->
                  createEncryptedUniverseKeyBackupTask(paramEntry)
                      .setSubTaskGroupType(subTaskGroupType));
      createTableBackupTaskYb(backupTableParams).setSubTaskGroupType(subTaskGroupType);
    }

    if (backupRequestParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(subTaskGroupType);
    }

    if (ybcBackup) {
      createMarkYBCBackupSucceeded(backup.getCustomerUUID(), backup.getBackupUUID())
          .setSubTaskGroupType(subTaskGroupType);
    }

    return backup;
  }

  // Save restore category to task params.
  private void getAndSaveRestoreBackupCategory(
      RestoreBackupParams restoreParams, TaskInfo taskInfo) {
    Set<String> backupLocations =
        restoreParams.backupStorageInfoList.parallelStream()
            .map(bSI -> bSI.storageLocation)
            .collect(Collectors.toSet());
    boolean isYbc =
        backupHelper.checkFileExistsOnBackupLocation(
            restoreParams.storageConfigUUID,
            restoreParams.customerUUID,
            backupLocations,
            restoreParams.getUniverseUUID(),
            YbcBackupUtil.YBC_SUCCESS_MARKER_FILE_NAME,
            true);
    restoreParams.category = isYbc ? BackupCategory.YB_CONTROLLER : BackupCategory.YB_BACKUP_SCRIPT;
    // Update task params for this
    ObjectMapper mapper = new ObjectMapper();
    taskInfo.setDetails(mapper.valueToTree(restoreParams));
    taskInfo.save();
  }

  protected Restore createAllRestoreSubtasks(
      RestoreBackupParams restoreBackupParams, SubTaskGroupType subTaskGroupType) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(getUserTaskUUID());
    Universe universe = Universe.getOrBadRequest(restoreBackupParams.getUniverseUUID());
    Cluster pCluster = universe.getUniverseDetails().getPrimaryCluster();

    // No validation for xcluster/localProvider type tasks, since the backup
    // itself is used for populating restore task.
    if (taskInfo.getTaskType().equals(TaskType.RestoreBackup)
        && pCluster.userIntent.providerType != CloudType.local) {
      getAndSaveRestoreBackupCategory(restoreBackupParams, taskInfo);
      createPreflightValidateRestoreTask(restoreBackupParams)
          .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
    }
    if (restoreBackupParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subTaskGroupType);
    }

    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    boolean isYbc = restoreBackupParams.category.equals(BackupCategory.YB_CONTROLLER);

    if (!isYbc) {
      if (cloudType != CloudType.kubernetes) {
        // Ansible Configure Task for copying xxhsum binaries from
        // third_party directory to the DB nodes.
        installThirdPartyPackagesTask(universe)
            .setSubTaskGroupType(SubTaskGroupType.InstallingThirdPartySoftware);
      } else {
        installThirdPartyPackagesTaskK8s(
                universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.XXHSUM)
            .setSubTaskGroupType(SubTaskGroupType.InstallingThirdPartySoftware);
      }
    }

    if (isYbc) {
      String currentYbcTaskId = restoreBackupParams.currentYbcTaskId;
      int idx = 0;
      for (BackupStorageInfo backupStorageInfo : restoreBackupParams.backupStorageInfoList) {
        if (restoreBackupParams.currentIdx <= idx) {
          if (currentYbcTaskId == null) {
            RestoreBackupParams restoreKeyParams =
                BackupUtil.createRestoreKeyParams(restoreBackupParams, backupStorageInfo);
            if (restoreKeyParams != null) {
              createEncryptedUniverseKeyRestoreTaskYbc(restoreKeyParams)
                  .setSubTaskGroupType(subTaskGroupType);
            }
          }

          // Restore the data.
          RestoreBackupParams restoreDataParams =
              new RestoreBackupParams(
                  restoreBackupParams, backupStorageInfo, RestoreBackupParams.ActionType.RESTORE);
          createRestoreBackupYbcTask(restoreDataParams, idx).setSubTaskGroupType(subTaskGroupType);
        }
        idx++;
      }
    } else {
      for (BackupStorageInfo backupStorageInfo : restoreBackupParams.backupStorageInfoList) {
        RestoreBackupParams restoreKeyParams =
            BackupUtil.createRestoreKeyParams(restoreBackupParams, backupStorageInfo);
        if (restoreKeyParams != null) {
          createRestoreBackupTask(restoreKeyParams).setSubTaskGroupType(subTaskGroupType);
          createEncryptedUniverseKeyRestoreTaskYb(restoreKeyParams)
              .setSubTaskGroupType(subTaskGroupType);
        }
        // Restore the data.
        RestoreBackupParams restoreDataParams =
            new RestoreBackupParams(
                restoreBackupParams, backupStorageInfo, RestoreBackupParams.ActionType.RESTORE);
        createRestoreBackupTask(restoreDataParams).setSubTaskGroupType(subTaskGroupType);
      }
    }

    if (restoreBackupParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(subTaskGroupType);
    }

    Restore restore = null;
    if (restoreBackupParams.prefixUUID == null) {
      return restore;
    }
    Optional<Restore> restoreIfPresent = Restore.fetchRestore(restoreBackupParams.prefixUUID);
    if (restoreIfPresent.isPresent()) {
      restore = restoreIfPresent.get();
      restore.updateTaskUUID(getTaskUUID());
      restore.update(getTaskUUID(), Restore.State.InProgress);
    } else {
      log.info(
          "Creating entry for restore taskUUID: {}, restoreUUID: {} ",
          getTaskUUID(),
          restoreBackupParams.prefixUUID);
      restore = Restore.create(getTaskUUID(), restoreBackupParams);
    }

    return restore;
  }

  protected SubTaskGroup createCreatePitrConfigTask(
      Universe universe,
      String keyspaceName,
      TableType tableType,
      long retentionPeriodSeconds,
      long snapshotIntervalSeconds,
      @Nullable XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreatePitrConfig");
    CreatePitrConfigParams createPitrConfigParams = new CreatePitrConfigParams();
    createPitrConfigParams.setUniverseUUID(universe.getUniverseUUID());
    createPitrConfigParams.customerUUID = Customer.get(universe.getCustomerId()).getUuid();
    createPitrConfigParams.name = null;
    createPitrConfigParams.keyspaceName = keyspaceName;
    createPitrConfigParams.tableType = tableType;
    createPitrConfigParams.retentionPeriodInSeconds = retentionPeriodSeconds;
    createPitrConfigParams.xClusterConfig = xClusterConfig;
    createPitrConfigParams.intervalInSeconds = snapshotIntervalSeconds;

    CreatePitrConfig task = createTask(CreatePitrConfig.class);
    task.initialize(createPitrConfigParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createCreatePitrConfigTask(
      Universe universe,
      String keyspaceName,
      TableType tableType,
      long retentionPeriodSeconds,
      long snapshotIntervalSeconds) {
    return createCreatePitrConfigTask(
        universe,
        keyspaceName,
        tableType,
        retentionPeriodSeconds,
        snapshotIntervalSeconds,
        null /* xClusterConfig */);
  }

  protected SubTaskGroup createRestoreSnapshotScheduleTask(
      Universe universe, PitrConfig pitrConfig, long restoreTimeMs) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestoreSnapshotSchedule");
    RestoreSnapshotScheduleParams params = new RestoreSnapshotScheduleParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.pitrConfigUUID = pitrConfig.getUuid();
    params.restoreTimeInMillis = restoreTimeMs;
    RestoreSnapshotSchedule task = createTask(RestoreSnapshotSchedule.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeletePitrConfigTask(
      UUID pitrConfigUuid, UUID universeUUID, boolean ignoreErrors) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeletePitrConfig");
    DeletePitrConfig.Params deletePitrConfigParams = new DeletePitrConfig.Params();
    deletePitrConfigParams.setUniverseUUID(universeUUID);
    deletePitrConfigParams.pitrConfigUuid = pitrConfigUuid;
    deletePitrConfigParams.ignoreErrors = ignoreErrors;

    DeletePitrConfig task = createTask(DeletePitrConfig.class);
    task.initialize(deletePitrConfigParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup installThirdPartyPackagesTaskK8s(
      Universe universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType upgradeType) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("InstallingThirdPartySoftware");
    InstallThirdPartySoftwareK8s task = createTask(InstallThirdPartySoftwareK8s.class);
    InstallThirdPartySoftwareK8s.Params params = new InstallThirdPartySoftwareK8s.Params();
    params.universeUUID = universe.getUniverseUUID();
    params.softwareType = upgradeType;
    task.initialize(params);

    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createTableBackupTaskYb(BackupTableParams taskParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BackupTableYb", taskParams.ignoreErrors);
    BackupTableYb task = createTask(BackupTableYb.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createPreflightValidateBackupTask(
      BackupTableParams backupParams, boolean ybcBackup) {
    return createPreflightValidateBackupTask(
        backupParams.storageConfigUUID,
        backupParams.customerUuid,
        backupParams.getUniverseUUID(),
        ybcBackup);
  }

  public SubTaskGroup createPreflightValidateBackupTask(
      UUID storageConfigUUID, UUID customerUUID, UUID universeUUID, boolean ybcBackup) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BackupPreflightValidate");
    BackupPreflightValidate task = createTask(BackupPreflightValidate.class);
    BackupPreflightValidate.Params params =
        new BackupPreflightValidate.Params(
            storageConfigUUID, customerUUID, universeUUID, ybcBackup);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createPreflightValidateRestoreTask(RestoreBackupParams restoreParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestorePreflightValidate");
    RestorePreflightValidate task = createTask(RestorePreflightValidate.class);
    task.initialize(restoreParams);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createTableBackupTasksYbc(
      BackupTableParams backupParams, int parallelDBBackups) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BackupTableYbc");
    Universe universe = Universe.getOrBadRequest(backupParams.getUniverseUUID());
    YbcBackupNodeRetriever nodeRetriever = new YbcBackupNodeRetriever(universe, parallelDBBackups);
    nodeRetriever.initializeNodePoolForBackups(backupParams.backupDBStates);
    Backup previousBackup =
        (!backupParams.baseBackupUUID.equals(backupParams.backupUuid))
            ? Backup.getLastSuccessfulBackupInChain(
                backupParams.customerUuid, backupParams.baseBackupUUID)
            : null;
    backupParams.backupList.stream()
        .filter(
            paramsEntry ->
                !backupParams.backupDBStates.get(paramsEntry.backupParamsIdentifier)
                    .alreadyScheduled)
        .forEach(
            paramsEntry -> {
              BackupTableYbc task = createTask(BackupTableYbc.class);
              BackupTableYbc.Params backupYbcParams =
                  new BackupTableYbc.Params(paramsEntry, nodeRetriever, universe);
              backupYbcParams.previousBackup = previousBackup;
              backupYbcParams.nodeIp =
                  backupParams.backupDBStates.get(paramsEntry.backupParamsIdentifier).nodeIp;
              backupYbcParams.taskID =
                  backupParams.backupDBStates.get(paramsEntry.backupParamsIdentifier)
                      .currentYbcTaskId;
              task.initialize(backupYbcParams);
              task.setUserTaskUUID(getUserTaskUUID());
              subTaskGroup.addSubTask(task);
            });
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createRestoreBackupTask(RestoreBackupParams taskParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestoreBackupYb");
    RestoreBackupYb task = createTask(RestoreBackupYb.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createRestoreBackupYbcTask(RestoreBackupParams taskParams, int index) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestoreBackupYbc");
    RestoreBackupYbc task = createTask(RestoreBackupYbc.class);
    RestoreBackupYbc.Params restoreParams = new RestoreBackupYbc.Params(taskParams);
    restoreParams.index = index;
    task.initialize(restoreParams);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDeleteBackupTasks(List<Backup> backups, UUID customerUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteBackup");
    for (Backup backup : backups) {
      DeleteBackup.Params params = new DeleteBackup.Params();
      params.backupUUID = backup.getBackupUUID();
      params.customerUUID = customerUUID;
      DeleteBackup task = createTask(DeleteBackup.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDeleteBackupYbTasks(List<Backup> backups, UUID customerUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteBackupYb");
    for (Backup backup : backups) {
      DeleteBackupYb.Params params = new DeleteBackupYb.Params();
      params.backupUUID = backup.getBackupUUID();
      params.customerUUID = customerUUID;
      DeleteBackupYb task = createTask(DeleteBackupYb.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyBackupTask() {
    return createEncryptedUniverseKeyBackupTask((BackupTableParams) taskParams());
  }

  public SubTaskGroup createEncryptedUniverseKeyBackupTask(BackupTableParams params) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BackupUniverseKeys");
    BackupUniverseKeys task = createTask(BackupUniverseKeys.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyRestoreTask(BackupTableParams params) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestoreUniverseKeys");
    RestoreUniverseKeys task = createTask(RestoreUniverseKeys.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyRestoreTaskYb(RestoreBackupParams params) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestoreUniverseKeysYb");
    RestoreUniverseKeysYb task = createTask(RestoreUniverseKeysYb.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyRestoreTaskYbc(RestoreBackupParams params) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RestoreUniverseKeysYbc");
    RestoreUniverseKeysYbc task = createTask(RestoreUniverseKeysYbc.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to upgrade desired ybc version on a universe.
   *
   * @param universeUUID universe on which ybc need to be upgraded
   * @param ybcVersion desired ybc version
   * @param validateOnlyMasterLeader flag to check only if master leader node's ybc is upgraded or
   *     not
   */
  public SubTaskGroup createUpgradeYbcTask(
      UUID universeUUID, String ybcVersion, boolean validateOnlyMasterLeader) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpgradeYbc");
    UpgradeYbc task = createTask(UpgradeYbc.class);
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = universeUUID;
    params.ybcVersion = ybcVersion;
    params.validateOnlyMasterLeader = validateOnlyMasterLeader;
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to upgrade desired ybc version on a k8s universe.
   *
   * @param universeUUID universe on which ybc need to be upgraded
   * @param ybcSoftwareVersion desired ybc version not
   */
  public SubTaskGroup createUpgradeYbcTaskOnK8s(UUID universeUUID, String ybcSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpgradeYbc");
    InstallYbcSoftwareOnK8s task = createTask(InstallYbcSoftwareOnK8s.class);
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(universeUUID);
    params.setYbcSoftwareVersion(ybcSoftwareVersion);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to install xxhash on the DB nodes from third-party packages.
   *
   * @param universe universe on which xxhash needs to be installed
   */
  public SubTaskGroup installThirdPartyPackagesTask(Universe universe) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for nodes",
            SubTaskGroupType.InstallingThirdPartySoftware);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    List<NodeDetails> nodes = universe.getServers(ServerType.TSERVER);
    for (NodeDetails node : nodes) {
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      UserIntent userIntent =
          universe.getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params =
          getBaseAnsibleServerTaskParams(
              userIntent,
              node,
              ServerType.TSERVER,
              UpgradeTaskParams.UpgradeTaskType.ThirdPartyPackages,
              UpgradeTaskParams.UpgradeTaskSubType.InstallThirdPartyPackages);
      params.setUniverseUUID(universe.getUniverseUUID());
      params.installThirdPartyPackages = true;
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDnsManipulationTask(
      DnsManager.DnsCommandType eventType, boolean isForceDelete, Universe universe) {
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    return createDnsManipulationTask(eventType, isForceDelete, primaryCluster);
  }

  /**
   * Creates a task list to manipulate the DNS record available for this universe.
   *
   * @param eventType the type of manipulation to do on the DNS records.
   * @param isForceDelete if this is a delete operation, set this to true to ignore errors
   * @param primaryCluster primary cluster information.
   * @return subtask group
   */
  public SubTaskGroup createDnsManipulationTask(
      DnsManager.DnsCommandType eventType, boolean isForceDelete, Cluster primaryCluster) {
    UserIntent userIntent = primaryCluster.userIntent;
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateDnsEntry");
    Provider p = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    if (!p.getCloudCode().isHostedZoneEnabled()) {
      return subTaskGroup;
    }
    // TODO: shared constant with javascript land?
    String hostedZoneId = p.getHostedZoneId();
    if (hostedZoneId == null || hostedZoneId.isEmpty()) {
      return subTaskGroup;
    }
    ManipulateDnsRecordTask.Params params = new ManipulateDnsRecordTask.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.type = eventType;
    params.providerUUID = UUID.fromString(userIntent.provider);
    params.hostedZoneId = hostedZoneId;
    params.domainNamePrefix =
        String.format(
            "%s.%s", userIntent.universeName, Customer.get(p.getCustomerUUID()).getCode());
    params.isForceDelete = isForceDelete;
    // Create the task to update DNS entries.
    ManipulateDnsRecordTask task = createTask(ManipulateDnsRecordTask.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to update the placement information by making a call to the master leader
   * and adds it to the task queue.
   *
   * @param blacklistNodes list of nodes which are being removed.
   */
  public SubTaskGroup createPlacementInfoTask(Collection<NodeDetails> blacklistNodes) {
    return createPlacementInfoTask(blacklistNodes, null);
  }

  /**
   * Creates a task list to update the placement information by making a call to the master leader
   * and adds it to the task queue.
   *
   * @param blacklistNodes list of nodes which are being removed.
   * @param targetClusterStates new state of clusters (for the case when placement info is updated
   *     but not persisted in db)
   */
  public SubTaskGroup createPlacementInfoTask(
      Collection<NodeDetails> blacklistNodes, @Nullable List<Cluster> targetClusterStates) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdatePlacementInfo");
    UpdatePlacementInfo.Params params = new UpdatePlacementInfo.Params();
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.targetClusterStates = targetClusterStates;
    // Set the blacklist nodes if any are passed in.
    if (blacklistNodes != null && !blacklistNodes.isEmpty()) {
      Set<String> blacklistNodeNames = new HashSet<>();
      for (NodeDetails node : blacklistNodes) {
        blacklistNodeNames.add(node.nodeName);
      }
      params.blacklistNodes = blacklistNodeNames;
    }
    // Create the task to update placement info.
    UpdatePlacementInfo task = createTask(UpdatePlacementInfo.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to move the data out of blacklisted servers.
   *
   * @return the created task group.
   */
  public SubTaskGroup createWaitForDataMoveTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForDataMove");
    WaitForDataMove.Params params = new WaitForDataMove.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // Create the task.
    WaitForDataMove waitForMove = createTask(WaitForDataMove.class);
    waitForMove.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(waitForMove);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForLeaderBlacklistCompletionTask(int waitTimeMs) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForLeaderBlacklistCompletion");
    WaitForLeaderBlacklistCompletion.Params params = new WaitForLeaderBlacklistCompletion.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.waitTimeMs = waitTimeMs;
    // Create the task.
    WaitForLeaderBlacklistCompletion leaderBlacklistCompletion =
        createTask(WaitForLeaderBlacklistCompletion.class);
    leaderBlacklistCompletion.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(leaderBlacklistCompletion);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to wait for leaders to be on preferred regions only. */
  public void createWaitForLeadersOnPreferredOnlyTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForLeadersOnPreferredOnly");
    WaitForLeadersOnPreferredOnly.Params params = new WaitForLeadersOnPreferredOnly.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // Create the task.
    WaitForLeadersOnPreferredOnly waitForLeadersOnPreferredOnly =
        createTask(WaitForLeadersOnPreferredOnly.class);
    waitForLeadersOnPreferredOnly.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(waitForLeadersOnPreferredOnly);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    // Set the subgroup task type.
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.WaitForDataMigration);
  }

  /**
   * Creates a task to move the data onto any lesser loaded servers.
   *
   * @return the created task group.
   */
  public SubTaskGroup createWaitForLoadBalanceTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForLoadBalance");
    WaitForLoadBalance.Params params = new WaitForLoadBalance.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // Create the task.
    WaitForLoadBalance waitForLoadBalance = createTask(WaitForLoadBalance.class);
    waitForLoadBalance.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(waitForLoadBalance);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates tasks to gracefully stop processes on node.
   *
   * @param node node to stop processes.
   * @param processes set of processes to stop.
   * @param finalState indicates that process will be stopped for unknown amount of time.
   * @param removeMasterFromQuorum true if this stop is a for long time.
   * @param subTaskGroupType subtask group type.
   */
  protected void stopProcessesOnNode(
      NodeDetails node,
      Set<ServerType> processes,
      boolean finalState,
      boolean removeMasterFromQuorum,
      SubTaskGroupType subTaskGroupType) {
    if (processes.contains(ServerType.TSERVER)) {
      addLeaderBlackListIfAvailable(Collections.singletonList(node), subTaskGroupType);

      if (finalState) {
        // Remove node from load balancer.
        UniverseDefinitionTaskParams universeDetails = getUniverse().getUniverseDetails();
        createManageLoadBalancerTasks(
            createLoadBalancerMap(
                universeDetails,
                ImmutableList.of(universeDetails.getClusterByUuid(node.placementUuid)),
                ImmutableSet.of(node),
                null));
      }
    }
    for (ServerType processType : processes) {
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subTaskGroupType);
      if (processType == ServerType.MASTER && removeMasterFromQuorum) {
        createWaitForMasterLeaderTask().setSubTaskGroupType(subTaskGroupType);
        createChangeConfigTasks(node, false /* isAdd */, subTaskGroupType);
      }
    }
  }

  /**
   * Creates tasks to gracefully start processes on node
   *
   * @param node node to start processes.
   * @param processTypes set of processes to start.
   * @param subGroupType subtask group type.
   * @param addMasterToQuorum true if started for the first time (or after long stop).
   * @param wasStopped true if process was stopped before.
   * @param sleepTimeFunction if not null - function to calculate time to wait for process.
   */
  protected void startProcessesOnNode(
      NodeDetails node,
      Set<ServerType> processTypes,
      SubTaskGroupType subGroupType,
      boolean addMasterToQuorum,
      boolean wasStopped,
      @Nullable Function<ServerType, Integer> sleepTimeFunction) {
    for (ServerType processType : processTypes) {
      createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(Collections.singletonList(node), processType)
          .setSubTaskGroupType(subGroupType);
      if (processType == ServerType.MASTER && addMasterToQuorum) {
        // Add stopped master to the quorum.
        createChangeConfigTasks(node, true /* isAdd */, subGroupType);
      }
      if (sleepTimeFunction != null) {
        createWaitForServerReady(node, processType).setSubTaskGroupType(subGroupType);
      }
      if (wasStopped && processType == ServerType.TSERVER) {
        removeFromLeaderBlackListIfAvailable(Collections.singletonList(node), subGroupType);
      }
    }
  }

  /**
   * Creates a task to add nodes to leader blacklist on server if available and wait for completion.
   *
   * @param nodes Nodes that have to be added to the blacklist.
   * @param subTaskGroupType Sub task group type for tasks.
   * @return true if tasks were created.
   */
  public boolean addLeaderBlackListIfAvailable(
      Collection<NodeDetails> nodes, SubTaskGroupType subTaskGroupType) {
    if (modifyLeaderBlacklistIfAvailable(nodes, true, subTaskGroupType)) {
      createWaitForLeaderBlacklistCompletionTask(
              getOrCreateExecutionContext().leaderBacklistWaitTimeMs)
          .setSubTaskGroupType(subTaskGroupType);
      return true;
    }
    return false;
  }

  /**
   * Creates a task to remove nodes from leader blacklist on server if available.
   *
   * @param nodes Nodes that have to be removed from blacklist.
   * @param subTaskGroupType Sub task group type for tasks.
   * @return true if tasks were created.
   */
  public boolean removeFromLeaderBlackListIfAvailable(
      Collection<NodeDetails> nodes, SubTaskGroupType subTaskGroupType) {
    return modifyLeaderBlacklistIfAvailable(nodes, false, subTaskGroupType);
  }

  private boolean modifyLeaderBlacklistIfAvailable(
      Collection<NodeDetails> nodes, boolean isAdd, SubTaskGroupType subTaskGroupType) {
    if (isBlacklistLeaders()) {
      Collection<NodeDetails> availableToBlacklist =
          nodes.stream().filter(this::isLeaderBlacklistValidRF).collect(Collectors.toSet());
      if (availableToBlacklist.size() > 0) {
        createModifyBlackListTask(
                isAdd ? availableToBlacklist : null /* addNodes */,
                isAdd ? null : availableToBlacklist /* removeNodes */,
                true)
            .setSubTaskGroupType(subTaskGroupType);
        return true;
      }
    }
    return false;
  }

  protected void clearLeaderBlacklistIfAvailable(SubTaskGroupType subTaskGroupType) {
    removeFromLeaderBlackListIfAvailable(getUniverse().getTServers(), subTaskGroupType);
  }

  protected boolean isBlacklistLeaders() {
    return getOrCreateExecutionContext().isBlacklistLeaders();
  }

  protected boolean isFollowerLagCheckEnabled() {
    return getOrCreateExecutionContext().isFollowerLagCheckEnabled();
  }

  /**
   * Creates a task to add/remove nodes from blacklist on server.
   *
   * @param addNodes The nodes that have to be added to the blacklist.
   * @param removeNodes The nodes that have to be removed from the blacklist.
   * @param isLeaderBlacklist true if we are leader blacklisting the node
   */
  public SubTaskGroup createModifyBlackListTask(
      Collection<NodeDetails> addNodes,
      Collection<NodeDetails> removeNodes,
      boolean isLeaderBlacklist) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ModifyBlackList");
    ModifyBlackList.Params params = new ModifyBlackList.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.addNodes = addNodes;
    params.removeNodes = removeNodes;
    params.isLeaderBlacklist = isLeaderBlacklist;
    // Create the task.
    ModifyBlackList modifyBlackList = createTask(ModifyBlackList.class);
    modifyBlackList.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(modifyBlackList);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to add/remove nodes from load balancer.
   *
   * @param lbMap The mapping for each cluster (Provider UUID + Region + LB Name) -> List of nodes
   *     per AZ
   */
  public void createManageLoadBalancerTasks(Map<LoadBalancerPlacement, LoadBalancerConfig> lbMap) {
    if (MapUtils.isNotEmpty(lbMap)) {
      SubTaskGroup subTaskGroup = createSubTaskGroup("ManageLoadBalancerGroup");
      for (Map.Entry<LoadBalancerPlacement, LoadBalancerConfig> lb : lbMap.entrySet()) {
        LoadBalancerPlacement lbPlacement = lb.getKey();
        LoadBalancerConfig lbConfig = lb.getValue();
        ManageLoadBalancerGroup.Params params = new ManageLoadBalancerGroup.Params();
        // Add the universe uuid.
        params.setUniverseUUID(taskParams().getUniverseUUID());
        // Add the provider uuid.
        params.providerUUID = lbPlacement.getProviderUUID();
        // Add the region for this load balancer
        params.regionCode = lbPlacement.getRegionCode();
        // Add the load balancer nodes to be added/removed
        params.lbConfig = lbConfig;
        // Create and add a task for this load balancer
        ManageLoadBalancerGroup task = createTask(ManageLoadBalancerGroup.class);
        task.initialize(params);
        subTaskGroup.addSubTask(task);
      }
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
  }

  /**
   * Create Load Balancer map to add/remove nodes from load balancer.
   *
   * @param taskParams the universe task params.
   * @param targetClusters list of clusters with nodes that need to be added/removed. If null/empty,
   *     default to all clusters.
   * @param nodesToIgnore list of nodes to exclude.
   * @param nodesToAdd list of nodes to add that may not be updated in the taskParams by the time
   *     the map is created.
   * @return a map. Key is LoadBalancerPlacement (cloud provider uuid, region code, load balancer
   *     name) and value is LoadBalancerConfig (load balancer name, map of AZs and their list of
   *     nodes)
   */
  public Map<LoadBalancerPlacement, LoadBalancerConfig> createLoadBalancerMap(
      UniverseDefinitionTaskParams taskParams,
      List<Cluster> targetClusters,
      Set<NodeDetails> nodesToIgnore,
      Set<NodeDetails> nodesToAdd) {
    boolean allClusters = CollectionUtils.isEmpty(targetClusters);
    // Get load balancer map for target clusters
    Map<LoadBalancerPlacement, LoadBalancerConfig> targetLbMap =
        generateLoadBalancerMap(taskParams, targetClusters, nodesToIgnore, nodesToAdd);
    // Get load balancer map remaining clusters in universe
    List<Cluster> remainingClusters = taskParams.clusters;
    if (!allClusters) {
      remainingClusters =
          remainingClusters.stream()
              .filter(c -> !targetClusters.contains(c))
              .collect(Collectors.toList());
    }
    Map<LoadBalancerPlacement, LoadBalancerConfig> remainingLbMap =
        generateLoadBalancerMap(taskParams, remainingClusters, nodesToIgnore, nodesToAdd);

    // Filter by target load balancers and
    // merge nodes in other clusters that are part of the same load balancer
    for (Map.Entry<LoadBalancerPlacement, LoadBalancerConfig> lb : targetLbMap.entrySet()) {
      LoadBalancerPlacement lbPlacement = lb.getKey();
      LoadBalancerConfig lbConfig = lb.getValue();
      if (remainingLbMap.containsKey(lbPlacement)) {
        lbConfig.addAll(remainingLbMap.get(lbPlacement).getAzNodes());
      }
    }
    return (allClusters) ? remainingLbMap : targetLbMap;
  }

  /**
   * Generates Load Balancer map for a list of clusters.
   *
   * @param taskParams the universe task params.
   * @param clusters list of clusters.
   * @param nodesToIgnore list of nodes to exclude.
   * @param nodesToAdd list of nodes to add that may not be updated to show in the taskParams by the
   *     time the map is created.
   * @return a map. Key is LoadBalancerPlacement (cloud provider uuid, region code, load balancer
   *     name) and value is LoadBalancerConfig (load balancer name, map of AZs and their list of
   *     nodes)
   */
  public Map<LoadBalancerPlacement, LoadBalancerConfig> generateLoadBalancerMap(
      UniverseDefinitionTaskParams taskParams,
      List<Cluster> clusters,
      Set<NodeDetails> nodesToIgnore,
      Set<NodeDetails> nodesToAdd) {
    // Prov1 + Reg1 + LB1 -> AZ1 (n1, n2, n3,...), AZ2 (n4, n5), AZ3(nX)
    Map<LoadBalancerPlacement, LoadBalancerConfig> loadBalancerMap = new HashMap<>();
    if (CollectionUtils.isEmpty(clusters)) {
      return loadBalancerMap;
    }
    // Get load balancers for each cluster
    for (Cluster cluster : clusters) {
      if (cluster.userIntent.enableLB) {
        // Map AZ -> nodes for each cluster
        Map<AvailabilityZone, Set<NodeDetails>> azNodes = new HashMap<>();
        Set<NodeDetails> nodes =
            taskParams.getNodesInCluster(cluster.uuid).stream()
                .filter(n -> n.isActive() && n.isTserver)
                .collect(Collectors.toSet());
        // Ignore nodes
        Set<AvailabilityZone> ignoredAzs = new HashSet<>();
        if (CollectionUtils.isNotEmpty(nodesToIgnore)) {
          nodes =
              nodes.stream().filter(n -> !nodesToIgnore.contains(n)).collect(Collectors.toSet());
          for (NodeDetails n : nodesToIgnore) {
            AvailabilityZone az = AvailabilityZone.getOrBadRequest(n.azUuid);
            ignoredAzs.add(az);
          }
        }
        // Add new nodes
        if (CollectionUtils.isNotEmpty(nodesToAdd)) {
          nodes.addAll(nodesToAdd);
        }
        for (NodeDetails node : nodes) {
          AvailabilityZone az = AvailabilityZone.getOrBadRequest(node.azUuid);
          azNodes.computeIfAbsent(az, v -> new HashSet<>()).add(node);
        }
        PlacementInfo.PlacementCloud placementCloud = cluster.placementInfo.cloudList.get(0);
        UUID providerUUID = placementCloud.uuid;
        List<PlacementInfo.PlacementAZ> azList =
            PlacementInfoUtil.getAZsSortedByNumNodes(cluster.placementInfo);
        for (PlacementInfo.PlacementAZ placementAZ : azList) {
          String lbName = placementAZ.lbName;
          AvailabilityZone az = AvailabilityZone.getOrBadRequest(placementAZ.uuid);
          // Skip map creation if all nodes in entire Regions/AZs have been ignored
          if (!Strings.isNullOrEmpty(lbName) && azNodes.containsKey(az)) {
            LoadBalancerPlacement lbPlacement =
                new LoadBalancerPlacement(providerUUID, az.getRegion().getCode(), lbName);
            LoadBalancerConfig lbConfig = new LoadBalancerConfig(lbName);
            loadBalancerMap
                .computeIfAbsent(lbPlacement, v -> lbConfig)
                .addNodes(az, azNodes.get(az));
          }
        }
        // Ensure removal of ignored nodes with PlacementAZs not in PlacementInfo
        Map<ClusterAZ, String> existingLBs = taskParams.existingLBs;
        if (MapUtils.isNotEmpty(existingLBs)) {
          for (AvailabilityZone az : ignoredAzs) {
            ClusterAZ clusterAZ = new ClusterAZ(cluster.uuid, az);
            if (existingLBs.containsKey(clusterAZ)) {
              String lbName = existingLBs.get(clusterAZ);
              LoadBalancerPlacement lbPlacement =
                  new LoadBalancerPlacement(providerUUID, az.getRegion().getCode(), lbName);
              loadBalancerMap.computeIfAbsent(lbPlacement, v -> new LoadBalancerConfig(lbName));
            }
          }
        }
      }
    }
    return loadBalancerMap;
  }

  public SubTaskGroup createUpdateMasterAddrsInMemoryTasks(
      Collection<NodeDetails> nodes, ServerType serverType) {
    return createSetFlagInMemoryTasks(
        nodes,
        serverType,
        (node, params) -> {
          params.force = true;
          params.updateMasterAddrs = true;
          params.masterAddrsOverride = getOrCreateExecutionContext().getMasterAddrsSupplier();
        });
  }

  // Subtask to update gflags in memory.
  public SubTaskGroup createSetFlagInMemoryTasks(
      Collection<NodeDetails> nodes,
      ServerType serverType,
      BiConsumer<NodeDetails, SetFlagInMemory.Params> paramCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("InMemoryGFlagUpdate");
    for (NodeDetails node : nodes) {
      // Create the task params.
      SetFlagInMemory.Params params = new SetFlagInMemory.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // The server type for the flag.
      params.serverType = serverType;
      paramCustomizer.accept(node, params);
      // Create the task.
      SetFlagInMemory setFlag = createTask(SetFlagInMemory.class);
      setFlag.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(setFlag);
    }
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to wait for a minimum number of tservers to heartbeat to the master leader.
   */
  public SubTaskGroup createWaitForTServerHeartBeatsTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForTServerHeartBeats");
    WaitForTServerHeartBeats task = createTask(WaitForTServerHeartBeats.class);
    WaitForTServerHeartBeats.Params params = new WaitForTServerHeartBeats.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Check if the node present in taskParams has a backing instance alive on the IaaS.
  public boolean instanceExists(NodeTaskParams taskParams) {
    ImmutableMap.Builder<String, String> expectedTags = ImmutableMap.builder();
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams.getNodeName());
    Cluster cluster = universe.getCluster(node.placementUuid);
    if (cluster.userIntent.providerType != CloudType.onprem) {
      expectedTags.put("universe_uuid", taskParams.getUniverseUUID().toString());
      if (taskParams.nodeUuid == null) {
        taskParams.nodeUuid = node.nodeUuid;
      }
      if (taskParams.nodeUuid != null) {
        expectedTags.put("node_uuid", taskParams.nodeUuid.toString());
      }
    }
    Optional<Boolean> optional = instanceExists(taskParams, expectedTags.build());
    if (!optional.isPresent()) {
      return false;
    }
    if (optional.get()) {
      return true;
    }
    // False means not matching the expected tags.
    throw new RuntimeException(
        String.format("Node %s already exist. Pick different universe name.", taskParams.nodeName));
  }

  // It returns 3 states - empty for not found, false for not matching and true for matching.
  public Optional<Boolean> instanceExists(
      NodeTaskParams taskParams, Map<String, String> expectedTags) {
    log.info("Expected tags: {}", expectedTags);
    ShellResponse response =
        nodeManager.nodeCommand(NodeManager.NodeCommandType.List, taskParams).processErrors();
    if (Strings.isNullOrEmpty(response.message)) {
      // Instance does not exist.
      return Optional.empty();
    }
    if (MapUtils.isEmpty(expectedTags)) {
      return Optional.of(true);
    }
    JsonNode jsonNode = Json.parse(response.message);
    if (jsonNode.isArray()) {
      jsonNode = jsonNode.get(0);
    }
    Map<String, JsonNode> properties =
        Streams.stream(jsonNode.fields())
            .collect(Collectors.toMap(e -> e.getKey(), e -> e.getValue()));
    int unmatchedCount = 0;
    for (Map.Entry<String, String> entry : expectedTags.entrySet()) {
      JsonNode node = properties.get(entry.getKey());
      if (node == null || node.isNull()) {
        continue;
      }
      String value = node.asText();
      log.info(
          "Node: {}, Key: {}, Value: {}, Expected: {}",
          taskParams.nodeName,
          entry.getKey(),
          value,
          entry.getValue());
      if (!entry.getValue().equals(value)) {
        unmatchedCount++;
      }
    }
    // Old nodes don't have tags. So, unmatched count is 0.
    // New nodes must have unmatched count = 0.
    return Optional.of(unmatchedCount == 0);
  }

  /**
   * Fetches the list of masters from the DB and checks if the master config change operation has
   * already been performed.
   *
   * @param universe Universe to query.
   * @param node Node to check.
   * @param isAddMasterOp True if the IP is to be added, false otherwise.
   * @param ipToUse IP to be checked.
   * @return true if it is already done, else false.
   */
  protected boolean isChangeMasterConfigDone(
      Universe universe, NodeDetails node, boolean isAddMasterOp, String ipToUse) {
    String masterAddresses = universe.getMasterAddresses();
    YBClient client = ybService.getClient(masterAddresses, universe.getCertificateNodetoNode());
    try {
      ListMastersResponse response = client.listMasters();
      List<ServerInfo> servers = response.getMasters();
      boolean anyMatched = servers.stream().anyMatch(s -> s.getHost().equals(ipToUse));
      return anyMatched == isAddMasterOp;
    } catch (Exception e) {
      String msg =
          String.format(
              "Error while performing master change config on node %s (%s:%d) - %s",
              node.nodeName, ipToUse, node.masterRpcPort, e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  // On master leader failover and tserver was already down, within the
  // "follower_unavailable_considered_failed_sec" time, the tserver will be instantly marked as
  // "dead" and not "live".
  public List<TabletServerInfo> getLiveTabletServers(Universe universe) {
    String masterAddresses = universe.getMasterAddresses();
    try (YBClient client =
        ybService.getClient(masterAddresses, universe.getCertificateNodetoNode())) {
      ListLiveTabletServersResponse response = client.listLiveTabletServers();

      return response.getTabletServers();
    } catch (Exception e) {
      String msg = String.format("Error while getting live tablet servers");
      throw new RuntimeException(msg, e);
    }
  }

  protected boolean isServerAlive(NodeDetails node, ServerType server, String masterAddrs) {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = ybService.getClient(masterAddrs, certificate);
    try {
      HostAndPort hp =
          HostAndPort.fromParts(
              node.cloudInfo.private_ip,
              server == ServerType.MASTER ? node.masterRpcPort : node.tserverRpcPort);
      return client.waitForServer(hp, 5000);
    } finally {
      ybService.closeClient(client, masterAddrs);
    }
  }

  public boolean isMasterAliveOnNode(NodeDetails node, String masterAddrs) {
    if (!node.isMaster) {
      return false;
    }
    return isServerAlive(node, ServerType.MASTER, masterAddrs);
  }

  public boolean isTserverAliveOnNode(NodeDetails node, String masterAddrs) {
    return isServerAlive(node, ServerType.TSERVER, masterAddrs);
  }

  public UniverseUpdater nodeStateUpdater(final String nodeName, final NodeStatus nodeStatus) {
    UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          NodeDetails node = universe.getNode(nodeName);
          if (node == null) {
            return;
          }
          NodeStatus currentStatus = NodeStatus.fromNode(node);
          log.info(
              "Changing node {} state from {} to {} in universe {}.",
              nodeName,
              currentStatus,
              nodeStatus,
              universe.getUniverseUUID());
          nodeStatus.fillNodeStates(node);
          if (nodeStatus.getNodeState() == NodeDetails.NodeState.Decommissioned) {
            node.cloudInfo.private_ip = null;
            node.cloudInfo.public_ip = null;
          }

          // Update the node details.
          universeDetails.nodeDetailsSet.add(node);
          universe.setUniverseDetails(universeDetails);
        };
    return updater;
  }

  // Helper API to update the db for the node with the given state.
  public void setNodeState(String nodeName, NodeDetails.NodeState state) {
    UniverseUpdater updater =
        nodeStateUpdater(nodeName, NodeStatus.builder().nodeState(state).build());
    saveUniverseDetails(updater);
  }

  // Return list of nodeNames from the given set of node details.
  public String nodeNames(Collection<NodeDetails> nodes) {
    StringBuilder nodeNames = new StringBuilder();
    for (NodeDetails node : nodes) {
      nodeNames.append(node.nodeName).append(",");
    }
    return nodeNames.substring(0, nodeNames.length() - 1);
  }

  /** Disable the loadbalancer to not move data. Used during rolling upgrades. */
  public SubTaskGroup createLoadBalancerStateChangeTask(boolean enable) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("LoadBalancerStateChange");
    LoadBalancerStateChange.Params params = new LoadBalancerStateChange.Params();
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.enable = enable;
    LoadBalancerStateChange task = createTask(LoadBalancerStateChange.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createUpdateUniverseSoftwareUpgradeStateTask(SoftwareUpgradeState state) {
    return createUpdateUniverseSoftwareUpgradeStateTask(
        state, null /* isSoftwareRollbackAllowed */);
  }

  /** Creates a task to update universe state */
  protected SubTaskGroup createUpdateUniverseSoftwareUpgradeStateTask(
      SoftwareUpgradeState state, Boolean isSoftwareRollbackAllowed) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateUniverseState");
    UpdateUniverseSoftwareUpgradeState.Params params =
        new UpdateUniverseSoftwareUpgradeState.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.state = state;
    params.isSoftwareRollbackAllowed = isSoftwareRollbackAllowed;
    UpdateUniverseSoftwareUpgradeState task = createTask(UpdateUniverseSoftwareUpgradeState.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Mark YBC backup state as completed and updates its expiry time. */
  public SubTaskGroup createMarkYBCBackupSucceeded(UUID customerUUID, UUID backupUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("MarkYBCBackupSucceed");
    YBCBackupSucceeded.Params params = new YBCBackupSucceeded.Params();
    params.customerUUID = customerUUID;
    params.backupUUID = backupUUID;
    YBCBackupSucceeded task = createTask(YBCBackupSucceeded.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createResetUniverseVersionTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ResetUniverseVersion");
    ResetUniverseVersion task = createTask(ResetUniverseVersion.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Whether to increment the universe/cluster config version. Skip incrementing version if the task
   * updating the universe metadata is create/destroy/pause/resume universe. Also, skip incrementing
   * version if task must manually handle version incrementing (such as in the case of XCluster).
   *
   * @return true if we should increment the version, false otherwise
   */
  protected boolean shouldIncrementVersion(UUID universeUuid) {
    Optional<Universe> universe = Universe.maybeGet(universeUuid);
    if (!universe.isPresent()) {
      return false;
    }

    final VersionCheckMode mode =
        confGetter.getConfForScope(universe.get(), UniverseConfKeys.universeVersionCheckMode);

    if (mode == VersionCheckMode.NEVER) {
      return false;
    }

    if (mode == VersionCheckMode.HA_ONLY && !HighAvailabilityConfig.get().isPresent()) {
      return false;
    }

    // For create/destroy/pause/resume operations, do not attempt to bump up
    // the cluster config version on the leader master because the cluster
    // and the leader master may not be available at the time we are attempting to do this.
    if (getUserTaskUUID() == null) {
      return false;
    }

    Optional<TaskInfo> optional = TaskInfo.maybeGet(getUserTaskUUID());
    if (!optional.isPresent()) {
      return false;
    }

    TaskType taskType = optional.get().getTaskType();
    return !(taskType == TaskType.CreateUniverse
        || taskType == TaskType.CreateKubernetesUniverse
        || taskType == TaskType.DestroyUniverse
        || taskType == TaskType.DestroyKubernetesUniverse
        || taskType == TaskType.PauseUniverse
        || taskType == TaskType.ResumeUniverse
        || taskType == TaskType.CreateXClusterConfig
        || taskType == TaskType.EditXClusterConfig
        || taskType == TaskType.SyncXClusterConfig
        || taskType == TaskType.DeleteXClusterConfig);
  }

  private int getClusterConfigVersion(Universe universe) {
    final String hostPorts = universe.getMasterAddresses();
    final String certificate = universe.getCertificateNodetoNode();
    int version;
    YBClient client = ybService.getClient(hostPorts, certificate);
    try {
      version = client.getMasterClusterConfig().getConfig().getVersion();
    } catch (Exception e) {
      log.error("Error occurred retrieving cluster config version", e);
      throw new RuntimeException("Error incrementing cluster config version", e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
    return version;
  }

  private boolean versionsMatch(UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Universe.UNIVERSE_KEY_LOCK.acquireLock(universeUUID);
    try {
      final int clusterConfigVersion = getClusterConfigVersion(universe);
      // For backwards compatibility (see V56__Alter_Universe_Version.sql)
      if (universe.getVersion() == -1) {
        universe.setVersion(clusterConfigVersion);
        log.info(
            "Updating version for universe {} from -1 to cluster config version {}",
            universeUUID,
            universe.getVersion());
        universe.save();
      }
      return universe.getVersion() == clusterConfigVersion;
    } finally {
      Universe.UNIVERSE_KEY_LOCK.releaseLock(universeUUID);
    }
  }

  /**
   * checkUniverseVersion
   *
   * @param universeUUID
   *     <p>Check that the universe version in the Platform database matches the one in the cluster
   *     config on the yugabyte db master. A mismatch could indicate one of two issues: 1. Multiple
   *     Platform replicas in a HA config are operating on the universe and (async) replication has
   *     failed to sychronize Platform db state correctly across different Platforms. We want to
   *     flag this case. 2. Manual yb-admin operations on the cluster have bumped up the database
   *     cluster config version. This is not necessarily always a problem, so we choose to ignore
   *     this case for now. When we get to a point where manual yb-admin operations are never
   *     needed, we can consider flagging this case. For now, we will let the universe version on
   *     Platform and the cluster config version on the master diverge.
   * @param mode version check mode
   */
  private void checkUniverseVersion(UUID universeUUID, VersionCheckMode mode) {
    if (mode == VersionCheckMode.NEVER) {
      return;
    }

    if (mode == VersionCheckMode.HA_ONLY && !HighAvailabilityConfig.get().isPresent()) {
      log.debug("Skipping cluster config version check for universe {}", universeUUID);
      return;
    }

    if (!versionsMatch(universeUUID)) {
      throw new RuntimeException("Universe version does not match cluster config version");
    }
  }

  protected void checkUniverseVersion() {
    checkUniverseVersion(
        taskParams().getUniverseUUID(),
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.universeVersionCheckMode));
  }

  /** Increment the cluster config version */
  private synchronized void incrementClusterConfigVersion(UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    final String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = ybService.getClient(hostPorts, certificate);
    try {
      int version = universe.getVersion();
      ModifyClusterConfigIncrementVersion modifyConfig =
          new ModifyClusterConfigIncrementVersion(client, version);
      int newVersion = modifyConfig.incrementVersion();
      log.info(
          "Updated cluster config version for universe {} from {} to {}",
          universeUUID,
          version,
          newVersion);
    } catch (Exception e) {
      log.error(
          "Error occurred incrementing cluster config version for universe " + universeUUID, e);
      throw new RuntimeException("Error incrementing cluster config version", e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  /**
   * Run universe updater and increment the cluster config version
   *
   * @param updater the universe updater to run
   * @return the updated universe
   */
  protected Universe saveUniverseDetails(
      UUID universeUUID, boolean shouldIncrementVersion, UniverseUpdater updater) {
    Universe.UNIVERSE_KEY_LOCK.acquireLock(universeUUID);
    try {
      if (updater.getConfig().isIgnoreAbsence() && !Universe.maybeGet(universeUUID).isPresent()) {
        return null;
      }
      if (shouldIncrementVersion) {
        incrementClusterConfigVersion(universeUUID);
      }
      return Universe.saveDetails(universeUUID, updater, shouldIncrementVersion);
    } finally {
      Universe.UNIVERSE_KEY_LOCK.releaseLock(universeUUID);
    }
  }

  protected Universe saveUniverseDetails(UUID universeUUID, UniverseUpdater updater) {
    return saveUniverseDetails(universeUUID, shouldIncrementVersion(universeUUID), updater);
  }

  protected Universe saveUniverseDetails(UniverseUpdater updater) {
    return saveUniverseDetails(taskParams().getUniverseUUID(), updater);
  }

  protected void saveNodeStatus(String nodeName, NodeStatus status) {
    saveUniverseDetails(nodeStateUpdater(nodeName, status));
  }

  protected void preTaskActions() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    preTaskActions(universe);
  }

  // Use this if it is already in transaction or the field changes are not yet written to the DB.
  protected void preTaskActions(Universe universe) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if ((details != null) && details.updateInProgress) {
      log.debug("Cancelling any active health-checks for universe {}", universe.getUniverseUUID());
      healthChecker.cancelHealthCheck(universe.getUniverseUUID());
    }
  }

  protected SubTaskGroup createRebootTasks(List<NodeDetails> nodes, boolean isHardReboot) {
    Class<? extends NodeTaskBase> taskClass =
        isHardReboot ? HardRebootServer.class : RebootServer.class;
    SubTaskGroup subTaskGroup = createSubTaskGroup(taskClass.getSimpleName());
    for (NodeDetails node : nodes) {
      NodeTaskParams params = isHardReboot ? new NodeTaskParams() : new RebootServer.Params();
      params.nodeName = node.nodeName;
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.azUuid = node.azUuid;

      NodeTaskBase task = createTask(taskClass);
      task.initialize(params);

      subTaskGroup.addSubTask(task);
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    return subTaskGroup;
  }

  public int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER
        ? taskParams().sleepAfterMasterRestartMillis
        : taskParams().sleepAfterTServerRestartMillis;
  }

  protected SubTaskGroup createWaitForClockSyncTasks(
      Universe universe,
      Collection<NodeDetails> nodes,
      long acceptableClockSkewNs,
      long subtaskTimeoutMs) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForClockSync");
    for (NodeDetails node : nodes) {
      WaitForClockSync.Params waitForClockSyncParams = new WaitForClockSync.Params();
      waitForClockSyncParams.setUniverseUUID(universe.getUniverseUUID());
      waitForClockSyncParams.nodeName = node.nodeName;
      waitForClockSyncParams.acceptableClockSkewNs = acceptableClockSkewNs;
      waitForClockSyncParams.subtaskTimeoutMs = subtaskTimeoutMs;

      WaitForClockSync waitForClockSyncTask = createTask(WaitForClockSync.class);
      waitForClockSyncTask.initialize(waitForClockSyncParams);
      subTaskGroup.addSubTask(waitForClockSyncTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createWaitForClockSyncTasks(
      Universe universe, Collection<NodeDetails> nodes) {
    return createWaitForClockSyncTasks(
        universe,
        nodes,
        this.confGetter
            .getGlobalConf(GlobalConfKeys.waitForClockSyncMaxAcceptableClockSkew)
            .toNanos(),
        this.confGetter.getGlobalConf(GlobalConfKeys.waitForClockSyncTimeout).toMillis());
  }

  protected SubTaskGroup createWaitForDurationSubtask(Universe universe, Duration waitTime) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForDuration");
    WaitForDuration.Params params = new WaitForDuration.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.waitTime = waitTime;

    WaitForDuration task = createTask(WaitForDuration.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It creates a map of keyspace name to keyspace ID for a specific table type by gathering the
   * list of NamespaceIdentifiers from a YBClient connected to a universe. The namespace name is
   * unique for a table type.
   *
   * @param client The client connected to the universe
   * @param tableType The table type for which you want the map
   * @return A map of keyspace name to keyspace ID
   */
  public static Map<String, String> getKeyspaceNameKeyspaceIdMap(
      YBClient client, CommonTypes.TableType tableType) {
    try {
      ListNamespacesResponse listNamespacesResponse = client.getNamespacesList();
      if (listNamespacesResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to get list of namespaces: %s", listNamespacesResponse.errorMessage()));
      }
      Map<String, String> keyspaceNameKeyspaceIdMap = new HashMap<>();
      listNamespacesResponse.getNamespacesList().stream()
          .map(NamespaceInfoResp::createFromNamespaceIdentifier)
          .filter(namespaceInfo -> namespaceInfo.tableType.equals(tableType))
          .forEach(
              namespaceInfo ->
                  keyspaceNameKeyspaceIdMap.put(
                      namespaceInfo.name, namespaceInfo.namespaceUUID.toString().replace("-", "")));
      return keyspaceNameKeyspaceIdMap;
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  // XCluster: All the xCluster related code resides in this section.
  // --------------------------------------------------------------------------------
  protected SubTaskGroup createXClusterConfigModifyTablesTask(
      XClusterConfig xClusterConfig,
      Set<String> tables,
      XClusterConfigModifyTables.Params.Action action) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigModifyTables");
    XClusterConfigModifyTables.Params modifyTablesParams = new XClusterConfigModifyTables.Params();
    modifyTablesParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    modifyTablesParams.xClusterConfig = xClusterConfig;
    modifyTablesParams.tables = tables;
    modifyTablesParams.action = action;

    XClusterConfigModifyTables task = createTask(XClusterConfigModifyTables.class);
    task.initialize(modifyTablesParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteReplicationTask(
      XClusterConfig xClusterConfig, boolean ignoreErrors) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteReplication");
    DeleteReplication.Params deleteReplicationParams = new DeleteReplication.Params();
    deleteReplicationParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    deleteReplicationParams.xClusterConfig = xClusterConfig;
    deleteReplicationParams.ignoreErrors = ignoreErrors;

    DeleteReplication task = createTask(DeleteReplication.class);
    task.initialize(deleteReplicationParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteBootstrapIdsTask(
      XClusterConfig xClusterConfig, Set<String> tableIds, boolean forceDelete) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteBootstrapIds");
    DeleteBootstrapIds.Params deleteBootstrapIdsParams = new DeleteBootstrapIds.Params();
    deleteBootstrapIdsParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    deleteBootstrapIdsParams.xClusterConfig = xClusterConfig;
    deleteBootstrapIdsParams.tableIds = tableIds;
    deleteBootstrapIdsParams.forceDelete = forceDelete;

    DeleteBootstrapIds task = createTask(DeleteBootstrapIds.class);
    task.initialize(deleteBootstrapIdsParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteXClusterConfigEntryTask(XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteXClusterConfigEntry");
    XClusterConfigTaskParams deleteXClusterConfigEntryParams = new XClusterConfigTaskParams();
    deleteXClusterConfigEntryParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    deleteXClusterConfigEntryParams.xClusterConfig = xClusterConfig;

    DeleteXClusterConfigEntry task = createTask(DeleteXClusterConfigEntry.class);
    task.initialize(deleteXClusterConfigEntryParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteXClusterTableConfigEntryTask(
      XClusterConfig xClusterConfig, Set<String> tableIds) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteXClusterTableConfigEntry");
    DeleteXClusterTableConfigEntry.Params params = new DeleteXClusterTableConfigEntry.Params();
    params.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    params.xClusterConfig = xClusterConfig;
    params.tableIds = tableIds;

    DeleteXClusterTableConfigEntry task = createTask(DeleteXClusterTableConfigEntry.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createPromoteSecondaryConfigToMainConfigTask(
      XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("PromoteSecondaryConfigToMainConfig");
    XClusterConfigTaskParams params = new XClusterConfigTaskParams();
    params.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    params.xClusterConfig = xClusterConfig;
    PromoteSecondaryConfigToMainConfig task = createTask(PromoteSecondaryConfigToMainConfig.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createResetXClusterConfigEntryTask(XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ResetXClusterConfigEntry");
    XClusterConfigTaskParams resetXClusterConfigEntryParams = new XClusterConfigTaskParams();
    resetXClusterConfigEntryParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    resetXClusterConfigEntryParams.xClusterConfig = xClusterConfig;

    ResetXClusterConfigEntry task = createTask(ResetXClusterConfigEntry.class);
    task.initialize(resetXClusterConfigEntryParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks(
      XClusterConfig xClusterConfig,
      String replicationGroupName,
      File sourceRootCertDirPath,
      boolean ignoreErrors) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("TransferXClusterCerts");
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    for (NodeDetails node : targetUniverse.getNodes()) {
      TransferXClusterCerts.Params transferParams = new TransferXClusterCerts.Params();
      transferParams.setUniverseUUID(targetUniverse.getUniverseUUID());
      transferParams.nodeName = node.nodeName;
      transferParams.azUuid = node.azUuid;
      transferParams.action = TransferXClusterCerts.Params.Action.REMOVE;
      transferParams.replicationGroupName = replicationGroupName;
      transferParams.producerCertsDirOnTarget = sourceRootCertDirPath;
      transferParams.ignoreErrors = ignoreErrors;

      TransferXClusterCerts transferXClusterCertsTask = createTask(TransferXClusterCerts.class);
      transferXClusterCertsTask.initialize(transferParams);
      subTaskGroup.addSubTask(transferXClusterCertsTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createChangeXClusterRoleTask(
      XClusterConfig xClusterConfig,
      @Nullable XClusterRole sourceRole,
      @Nullable XClusterRole targetRole) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ChangeXClusterRole");
    ChangeXClusterRole.Params ChangeXClusterRoleParams = new ChangeXClusterRole.Params();
    ChangeXClusterRoleParams.xClusterConfig = xClusterConfig;
    ChangeXClusterRoleParams.sourceRole = sourceRole;
    ChangeXClusterRoleParams.targetRole = targetRole;

    ChangeXClusterRole task = createTask(ChangeXClusterRole.class);
    task.initialize(ChangeXClusterRoleParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createSetDrStatesTask(
      XClusterConfig xClusterConfig,
      @Nullable DrConfigStates.State drConfigState,
      @Nullable SourceUniverseState sourceUniverseState,
      @Nullable TargetUniverseState targetUniverseState,
      @Nullable String keyspacePending) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetDrStates");
    SetDrStates.Params params = new SetDrStates.Params();
    params.xClusterConfig = xClusterConfig;
    params.drConfigState = drConfigState;
    params.sourceUniverseState = sourceUniverseState;
    params.targetUniverseState = targetUniverseState;
    params.keyspacePending = keyspacePending;

    SetDrStates task = createTask(SetDrStates.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void createRemoveTableFromXClusterConfigSubtasks(
      XClusterConfig xClusterConfig, Set<String> tableIds, boolean keepEntry) {
    // Remove the tables from the replication group.
    createXClusterConfigModifyTablesTask(
            xClusterConfig, tableIds, XClusterConfigModifyTables.Params.Action.REMOVE)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

    // Delete bootstrap IDs created by bootstrap universe subtask.
    createDeleteBootstrapIdsTask(xClusterConfig, tableIds, false /* forceDelete */)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

    if (!keepEntry) {
      // Delete the xCluster table configs from DB.
      createDeleteXClusterTableConfigEntryTask(xClusterConfig, tableIds);
    }
  }

  protected void createDeleteXClusterConfigSubtasks(
      XClusterConfig xClusterConfig,
      boolean keepEntry,
      boolean forceDelete,
      boolean deletePitrConfigs) {
    // If target universe is destroyed, ignore creating this subtask.
    if (xClusterConfig.getTargetUniverseUUID() != null
        && xClusterConfig.getType().equals(ConfigType.Txn)) {
      // Set back the target universe role to Active.
      createChangeXClusterRoleTask(
              xClusterConfig, null /* sourceRole */, XClusterRole.ACTIVE /* targetRole */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
    }

    // Delete the replication CDC streams on the target universe.
    createDeleteReplicationTask(xClusterConfig, forceDelete)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

    // Delete bootstrap IDs created by bootstrap universe subtask.
    createDeleteBootstrapIdsTask(xClusterConfig, xClusterConfig.getTableIds(), forceDelete)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

    if (deletePitrConfigs
        && xClusterConfig.getType().equals(ConfigType.Txn)
        && xClusterConfig.getTargetUniverseUUID() != null) {
      List<PitrConfig> pitrConfigs = xClusterConfig.getPitrConfigs();
      for (PitrConfig pitrConfig : pitrConfigs) {
        createDeletePitrConfigTask(
            pitrConfig.getUuid(),
            xClusterConfig.getTargetUniverseUUID(),
            forceDelete /* ignoreErrors */);
      }
    }

    // If target universe is destroyed, ignore creating this subtask.
    if (xClusterConfig.getTargetUniverseUUID() != null
        && (config.getBoolean(TransferXClusterCerts.K8S_TLS_SUPPORT_CONFIG_KEY)
            || !Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID())
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .providerType
                .equals(CloudType.kubernetes))) {
      File sourceRootCertDirPath =
          Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID())
              .getUniverseDetails()
              .getSourceRootCertDirPath();
      // Delete the source universe root cert from the target universe if it is transferred.
      if (sourceRootCertDirPath != null) {
        createTransferXClusterCertsRemoveTasks(
                xClusterConfig,
                xClusterConfig.getReplicationGroupName(),
                sourceRootCertDirPath,
                forceDelete
                    || xClusterConfig.getStatus()
                        == XClusterConfig.XClusterConfigStatusType.DeletedUniverse)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
      }
    }

    if (keepEntry) {
      createResetXClusterConfigEntryTask(xClusterConfig)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
    } else {
      // Delete the xCluster config from DB.
      createDeleteXClusterConfigEntryTask(xClusterConfig)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
    }
  }

  protected SubTaskGroup createDeleteDrConfigEntryTask(DrConfig drConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteDrConfigEntry");
    DrConfigTaskParams params = new DrConfigTaskParams();
    params.setUniverseUUID(drConfig.getActiveXClusterConfig().getTargetUniverseUUID());
    params.setDrConfig(drConfig);

    DeleteDrConfigEntry task = createTask(DeleteDrConfigEntry.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It updates the source master addresses on the target universe cluster config for all xCluster
   * configs on the source universe.
   */
  public void createXClusterConfigUpdateMasterAddressesTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigUpdateMasterAddresses");
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBySourceUniverseUUID(taskParams().getUniverseUUID()).stream()
            .filter(xClusterConfig -> !XClusterConfigTaskBase.isInMustDeleteStatus(xClusterConfig))
            .collect(Collectors.toList());
    Set<UUID> updatedTargetUniverses = new HashSet<>();
    for (XClusterConfig config : xClusterConfigs) {
      UUID targetUniverseUUID = config.getTargetUniverseUUID();
      // Each target universe needs to be updated only once, even though there could be several
      // xCluster configs between each source and target universe pair.
      if (updatedTargetUniverses.contains(targetUniverseUUID)) {
        continue;
      }
      updatedTargetUniverses.add(targetUniverseUUID);

      XClusterConfigUpdateMasterAddresses.Params params =
          new XClusterConfigUpdateMasterAddresses.Params();
      // Set the target universe UUID to be told the new master addresses.
      params.setUniverseUUID(targetUniverseUUID);
      // Set the source universe UUID to get the new master addresses.
      params.sourceUniverseUuid = taskParams().getUniverseUUID();

      XClusterConfigUpdateMasterAddresses task =
          createTask(XClusterConfigUpdateMasterAddresses.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    if (subTaskGroup.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
  }

  /**
   * It checks if it is necessary to copy the source universe root certificate to the target
   * universe for the xCluster replication config to work. If it is necessary, an optional
   * containing the path to the source root certificate on the Platform host will be returned.
   * Otherwise, it will be empty.
   *
   * @param sourceUniverse The source Universe in the xCluster replication config
   * @param targetUniverse The target Universe in the xCluster replication config
   * @return An optional File that is present if transferring the source root certificate is
   *     necessary
   * @throws IllegalArgumentException If setting up a replication config between a universe with
   *     node-to-node TLS and one without; It is not supported by coreDB
   */
  public static Optional<File> getSourceCertificateIfNecessary(
      Universe sourceUniverse, Universe targetUniverse) {
    String sourceCertificatePath = sourceUniverse.getCertificateNodetoNode();
    String targetCertificatePath = targetUniverse.getCertificateNodetoNode();

    if (sourceCertificatePath == null && targetCertificatePath == null) {
      return Optional.empty();
    }
    if (sourceCertificatePath != null && targetCertificatePath != null) {
      UniverseDefinitionTaskParams targetUniverseDetails = targetUniverse.getUniverseDetails();
      UniverseDefinitionTaskParams.UserIntent userIntent =
          targetUniverseDetails.getPrimaryCluster().userIntent;
      // If the "certs_for_cdc_dir" gflag is set, it must be set on masters and tservers with the
      // same value.
      String gflagValueOnMasters =
          userIntent.masterGFlags.get(XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG);
      String gflagValueOnTServers =
          userIntent.tserverGFlags.get(XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG);
      if ((gflagValueOnMasters != null || gflagValueOnTServers != null)
          && !java.util.Objects.equals(gflagValueOnMasters, gflagValueOnTServers)) {
        throw new IllegalStateException(
            String.format(
                "The %s gflag must "
                    + "be set on masters and tservers with the same value or not set at all: "
                    + "gflagValueOnMasters: %s, gflagValueOnTServers: %s",
                XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG,
                gflagValueOnMasters,
                gflagValueOnTServers));
      }
      // If the "certs_for_cdc_dir" gflag is set on the target universe, the certificate must
      // be transferred even though the universes are using the same certs.
      if (!sourceCertificatePath.equals(targetCertificatePath)
          || gflagValueOnMasters != null
          || targetUniverseDetails.xClusterInfo.isSourceRootCertDirPathGflagConfigured()) {
        File sourceCertificate = new File(sourceCertificatePath);
        if (!sourceCertificate.exists()) {
          throw new IllegalStateException(
              String.format("sourceCertificate file \"%s\" does not exist", sourceCertificate));
        }
        return Optional.of(sourceCertificate);
      }
      // The "certs_for_cdc_dir" gflag is not set and certs are equal, so the target universe does
      // not need the source cert.
      return Optional.empty();
    }
    throw new IllegalArgumentException(
        "A replication config cannot be set between a universe with node-to-node encryption "
            + "enabled and a universe with node-to-node encryption disabled.");
  }

  protected SubTaskGroup createTransferXClusterCertsCopyTasks(
      XClusterConfig xClusterConfig,
      Collection<NodeDetails> nodes,
      String replicationGroupName,
      File certificate,
      File sourceRootCertDirPath) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("TransferXClusterCerts");
    log.debug(
        "Creating subtasks to transfer {} to {} on nodes {} in universe {}",
        certificate,
        sourceRootCertDirPath,
        nodes.stream().map(node -> node.nodeName).collect(Collectors.toSet()),
        taskParams().getUniverseUUID());
    for (NodeDetails node : nodes) {
      TransferXClusterCerts.Params transferParams = new TransferXClusterCerts.Params();
      transferParams.setUniverseUUID(taskParams().getUniverseUUID());
      transferParams.nodeName = node.nodeName;
      transferParams.azUuid = node.azUuid;
      transferParams.rootCertPath = certificate;
      transferParams.action = TransferXClusterCerts.Params.Action.COPY;
      transferParams.replicationGroupName = replicationGroupName;
      transferParams.producerCertsDirOnTarget = sourceRootCertDirPath;
      transferParams.ignoreErrors = false;
      // sshPortOverride, in case the passed imageBundle has a different port
      // configured for the region.
      transferParams.sshPortOverride = node.sshPortOverride;

      TransferXClusterCerts transferXClusterCertsTask = createTask(TransferXClusterCerts.class);
      transferXClusterCertsTask.initialize(transferParams);
      subTaskGroup.addSubTask(transferXClusterCertsTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void createTransferXClusterCertsCopyTasks(
      Collection<NodeDetails> nodes, Universe targetUniverse, SubTaskGroupType subTaskGroupType) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByTargetUniverseUUID(targetUniverse.getUniverseUUID()).stream()
            .filter(xClusterConfig -> !XClusterConfigTaskBase.isInMustDeleteStatus(xClusterConfig))
            .collect(Collectors.toList());

    xClusterConfigs.forEach(
        xClusterConfig -> {
          Optional<File> sourceCertificate =
              getSourceCertificateIfNecessary(
                  Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID()), targetUniverse);
          sourceCertificate.ifPresent(
              cert ->
                  createTransferXClusterCertsCopyTasks(
                          xClusterConfig,
                          nodes,
                          xClusterConfig.getReplicationGroupName(),
                          cert,
                          targetUniverse.getUniverseDetails().getSourceRootCertDirPath())
                      .setSubTaskGroupType(subTaskGroupType));
        });
  }

  protected SubTaskGroup createXClusterInfoPersistTask(
      UniverseDefinitionTaskParams.XClusterInfo xClusterInfo) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterInfoPersist");
    XClusterInfoPersist.Params xClusterInfoPersistParams = new XClusterInfoPersist.Params();
    xClusterInfoPersistParams.setUniverseUUID(taskParams().getUniverseUUID());
    xClusterInfoPersistParams.xClusterInfo = xClusterInfo;

    XClusterInfoPersist task = createTask(XClusterInfoPersist.class);
    task.initialize(xClusterInfoPersistParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterInfoPersistTask() {
    return createXClusterInfoPersistTask(getUniverse().getUniverseDetails().xClusterInfo);
  }

  protected void unlockXClusterUniverses(
      Set<UUID> lockedXClusterUniversesUuidSet, boolean ignoreErrors) {
    if (lockedXClusterUniversesUuidSet == null) {
      return;
    }
    Exception firstException = null;
    for (UUID universeUuid : lockedXClusterUniversesUuidSet) {
      try {
        // Unlock the universe.
        unlockUniverseForUpdate(universeUuid);
      } catch (Exception e) {
        // Log the error message, and continue to unlock as many universes as possible.
        log.error(
            "{} hit error : could not unlock universe {} that was locked because of "
                + "participating in an XCluster config: {}",
            getName(),
            universeUuid,
            e.getMessage());
        if (firstException == null) {
          firstException = e;
        }
      }
    }
    if (firstException != null) {
      if (!ignoreErrors) {
        throw new RuntimeException(firstException);
      } else {
        log.debug("Error ignored");
      }
    }
  }

  protected void createFinalizeUpgradeTasks(boolean upgradeSystemCatalog) {
    Universe universe = getUniverse();
    String version = universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

    createUpdateUniverseSoftwareUpgradeStateTask(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Finalizing,
        false /* isSoftwareRollbackAllowed */);

    if (!confGetter.getConfForScope(universe, UniverseConfKeys.skipUpgradeFinalize)) {
      if (upgradeSystemCatalog) {
        // Run YSQL upgrade on the universe.
        createRunYsqlUpgradeTask(version);
      }
      // Promote all auto flags upto class External.
      createPromoteAutoFlagTask(
          universe.getUniverseUUID(),
          true /* ignoreErrors */,
          AutoFlagUtil.EXTERNAL_AUTO_FLAG_CLASS_NAME /* maxClass */);

      createUpdateUniverseSoftwareUpgradeStateTask(
          UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready);

    } else {
      log.info("Skipping upgrade finalization for universe : " + universe.getUniverseUUID());
    }
  }

  protected void createPromoteAutoFlagsAndLockOtherUniverse(
      Universe universe, Set<UUID> alreadyLockedUniverseUUIDSet, boolean ignoreErrors) {
    if (lockedXClusterUniversesUuidSet == null) {
      lockedXClusterUniversesUuidSet = new HashSet<>();
    }
    // Lock the other universe if it is not locked already.
    if (!(lockedXClusterUniversesUuidSet.contains(universe.getUniverseUUID())
        || alreadyLockedUniverseUUIDSet.contains(universe.getUniverseUUID()))) {
      lockedXClusterUniversesUuidSet =
          Sets.union(
              lockedXClusterUniversesUuidSet, Collections.singleton(universe.getUniverseUUID()));
      if (lockUniverseIfExist(universe.getUniverseUUID(), -1 /* expectedUniverseVersion */)
          == null) {
        log.info("universe is deleted; No further action is needed");
        return;
      }
    }
    // Create subtask to promote autoFlags on the universe.
    createPromoteAutoFlagTask(universe.getUniverseUUID(), ignoreErrors)
        .setSubTaskGroupType(SubTaskGroupType.PromoteAutoFlags);
  }

  protected void createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
      Set<UUID> xClusterConnectedUniverseSet,
      Set<UUID> alreadyLockedUniverseUUIDSet,
      XClusterUniverseService xClusterUniverseService,
      Set<UUID> excludeXClusterConfigSet,
      boolean ignoreErrors) {
    createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
        xClusterConnectedUniverseSet,
        alreadyLockedUniverseUUIDSet,
        xClusterUniverseService,
        excludeXClusterConfigSet,
        null /* univUpgradeInProgress */,
        null /* upgradeUniverseSoftwareVersion */,
        ignoreErrors);
  }

  protected void createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
      Set<UUID> xClusterConnectedUniverseSet,
      Set<UUID> alreadyLockedUniverseUUIDSet,
      XClusterUniverseService xClusterUniverseService,
      Set<UUID> excludeXClusterConfigSet) {
    createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
        xClusterConnectedUniverseSet,
        alreadyLockedUniverseUUIDSet,
        xClusterUniverseService,
        excludeXClusterConfigSet,
        null /* univUpgradeInProgress */,
        null /* upgradeUniverseSoftwareVersion */,
        false /* ignoreErrors */);
  }

  protected void createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
      Set<UUID> xClusterConnectedUniverseSet,
      Set<UUID> alreadyLockedUniverseUUIDSet,
      XClusterUniverseService xClusterUniverseService,
      Set<UUID> excludeXClusterConfigSet,
      @Nullable Universe univUpgradeInProgress,
      @Nullable String upgradeUniverseSoftwareVersion) {
    createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
        xClusterConnectedUniverseSet,
        alreadyLockedUniverseUUIDSet,
        xClusterUniverseService,
        excludeXClusterConfigSet,
        univUpgradeInProgress,
        upgradeUniverseSoftwareVersion,
        false /* ignoreErrors */);
  }

  protected void createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
      Set<UUID> xClusterConnectedUniverseSet,
      Set<UUID> alreadyLockedUniverseUUIDSet,
      XClusterUniverseService xClusterUniverseService,
      Set<UUID> excludeXClusterConfigSet,
      @Nullable Universe univUpgradeInProgress,
      @Nullable String upgradeUniverseSoftwareVersion,
      boolean ignoreErrors) {
    // Fetch all separate xCluster connected universe group and promote auto flags
    // if possible.
    xClusterUniverseService
        .getMultipleXClusterConnectedUniverseSet(
            xClusterConnectedUniverseSet, excludeXClusterConfigSet)
        .stream()
        .filter(universeSet -> !CollectionUtils.isEmpty(universeSet))
        .forEach(
            universeSet -> {
              Universe universe = universeSet.stream().findFirst().get();
              String softwareVersion =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
              if (!StringUtils.isEmpty(upgradeUniverseSoftwareVersion)
                  && Objects.nonNull(univUpgradeInProgress)) {
                if (universeSet.stream()
                    .anyMatch(
                        univ ->
                            univ.getUniverseUUID()
                                .equals(univUpgradeInProgress.getUniverseUUID()))) {
                  universe = univUpgradeInProgress;
                  softwareVersion = upgradeUniverseSoftwareVersion;
                }
              }
              if (CommonUtils.isAutoFlagSupported(softwareVersion)) {
                try {
                  if (xClusterUniverseService.canPromoteAutoFlags(
                      universeSet, universe, softwareVersion)) {
                    universeSet.forEach(
                        univ ->
                            createPromoteAutoFlagsAndLockOtherUniverse(
                                univ, alreadyLockedUniverseUUIDSet, ignoreErrors));
                  }
                } catch (IOException e) {
                  throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
                }
              }
            });
  }
  // --------------------------------------------------------------------------------
  // End of XCluster.
}
