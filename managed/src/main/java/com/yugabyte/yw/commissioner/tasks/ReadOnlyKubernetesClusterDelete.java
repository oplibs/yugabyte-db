/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Slf4j
public class ReadOnlyKubernetesClusterDelete extends KubernetesTaskBase {

  @Inject
  public ReadOnlyKubernetesClusterDelete(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public UUID clusterUUID;
    public Boolean isForceDelete = false;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = null;
      if (params().isForceDelete) {
        universe = forceLockUniverseForUpdate(-1);
      } else {
        universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      }

      List<Cluster> roClusters = universe.getUniverseDetails().getReadOnlyClusters();
      if (CollectionUtils.isEmpty(roClusters)) {
        String msg =
            String.format(
                "Unable to delete ReadOnly cluster from universe %s as "
                    + "it doesn't have any ReadOnly clusters.",
                universe.name);
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      // We support only one readonly cluster, so using the first one in the list.
      Cluster cluster = roClusters.get(0);
      UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;
      UUID providerUUID = UUID.fromString(userIntent.provider);

      Map<String, String> universeConfig = universe.getConfig();
      boolean runHelmDelete = universeConfig.containsKey(Universe.HELM2_LEGACY);

      PlacementInfo pi = cluster.placementInfo;

      Provider provider = Provider.get(providerUUID);

      Map<UUID, Map<String, String>> azToConfig = PlacementInfoUtil.getConfigPerAZ(pi);

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

      SubTaskGroup helmDeletes =
          getTaskExecutor()
              .createSubTaskGroup(
                  KubernetesCommandExecutor.CommandType.HELM_DELETE.getSubTaskGroupName(),
                  executor);
      helmDeletes.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      SubTaskGroup volumeDeletes =
          getTaskExecutor()
              .createSubTaskGroup(
                  KubernetesCommandExecutor.CommandType.VOLUME_DELETE.getSubTaskGroupName(),
                  executor);
      volumeDeletes.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      SubTaskGroup namespaceDeletes =
          getTaskExecutor()
              .createSubTaskGroup(
                  KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE.getSubTaskGroupName(),
                  executor);
      namespaceDeletes.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

      for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
        UUID azUUID = entry.getKey();
        String azName = isMultiAz ? AvailabilityZone.get(azUUID).code : null;

        Map<String, String> config = entry.getValue();

        String namespace = config.get("KUBENAMESPACE");

        if (runHelmDelete || namespace != null) {
          // Delete the helm deployments.
          helmDeletes.addSubTask(
              createDestroyKubernetesTask(
                  universe.getUniverseDetails().nodePrefix,
                  azName,
                  config,
                  KubernetesCommandExecutor.CommandType.HELM_DELETE,
                  providerUUID,
                  newNamingStyle,
                  /*isReadOnlyCluster*/ true));
        }

        // Delete the PVCs created for this AZ.
        volumeDeletes.addSubTask(
            createDestroyKubernetesTask(
                universe.getUniverseDetails().nodePrefix,
                azName,
                config,
                KubernetesCommandExecutor.CommandType.VOLUME_DELETE,
                providerUUID,
                newNamingStyle,
                /*isReadOnlyCluster*/ true));

        // Delete the namespaces of the deployments only if those were
        // created by us.
        if (namespace == null) {
          namespaceDeletes.addSubTask(
              createDestroyKubernetesTask(
                  universe.getUniverseDetails().nodePrefix,
                  azName,
                  config,
                  KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE,
                  providerUUID,
                  newNamingStyle,
                  /*isReadOnlyCluster*/ true));
        }
      }

      getRunnableTask().addSubTaskGroup(helmDeletes);
      getRunnableTask().addSubTaskGroup(volumeDeletes);
      getRunnableTask().addSubTaskGroup(namespaceDeletes);

      // Remove the cluster entry from the universe db entry.
      createDeleteClusterFromUniverseTask(params().clusterUUID)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      // Remove the async_replicas in the cluster config on master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  // TODO this method is present in DestroyKubernetesUniverse.java also
  // RFC: Should we consider creating a base class and move it there?
  protected KubernetesCommandExecutor createDestroyKubernetesTask(
      String nodePrefix,
      String az,
      Map<String, String> config,
      KubernetesCommandExecutor.CommandType commandType,
      UUID providerUUID,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.commandType = commandType;
    params.providerUUID = providerUUID;
    params.isReadOnlyCluster = isReadOnlyCluster;
    params.helmReleaseName =
        PlacementInfoUtil.getHelmReleaseName(nodePrefix, az, isReadOnlyCluster);

    if (config != null) {
      params.config = config;
      // This assumes that the config is az config. It is true in this
      // particular case, all callers just pass az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              nodePrefix, az, config, newNamingStyle, isReadOnlyCluster);
    }
    params.universeUUID = taskParams().universeUUID;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }
}
