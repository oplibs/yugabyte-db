// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.Multimap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.tasks.params.IProviderTaskParams;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.Supplier;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@Singleton
@Slf4j
public class ProviderEditRestrictionManager {
  public static final String EDIT_PROVIDER_NEW_ENABLED_KEY = "yb.edit_provider.new.enabled";
  private static final long PROVIDER_LOCK_TIMEOUT_MILLIS = 30_000;
  private static final EnumSet<TaskInfo.State> ACTIVE_STATES =
      EnumSet.of(TaskInfo.State.Initializing, TaskInfo.State.Running, TaskInfo.State.Created);

  private final RuntimeConfigFactory runtimeConfigFactory;
  private final Map<UUID, ReentrantLock> providerLocks = new ConcurrentHashMap<>();
  private final Map<UUID, UUID> editTaskIdByProvider = new HashMap<>();
  private final Multimap<UUID, UUID> useTaskIdsByProvider = ArrayListMultimap.create();
  private final Map<UUID, Collection<UUID>> providersByTask = new ConcurrentHashMap<>();

  @Inject
  public ProviderEditRestrictionManager(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public void onTaskCreated(UUID taskId, ITask task, ITaskParams params) {
    log.debug("On task created {}  params {} ", taskId, params.getClass());
    if (!isEnabled()) {
      return;
    }
    Set<UUID> providerUUIDsToUse = getProviderUUIDsToUse(taskId, task, params);
    if (!providerUUIDsToUse.isEmpty()) {
      log.debug("Using {} providers with task {}", providerUUIDsToUse, taskId);
      providersByTask.put(taskId, providerUUIDsToUse);
      for (UUID providerUUID : new TreeSet<>(providerUUIDsToUse)) {
        tryUseProviderWithTask(providerUUID, taskId);
      }
    } else {
      Optional<UUID> providerUUIDopt = getProviderUUIDToEdit(taskId, task, params);
      if (providerUUIDopt.isPresent()) {
        log.debug("Edit providers {} for task {}", providerUUIDopt.get(), taskId);
        providersByTask.put(taskId, Collections.singleton(providerUUIDopt.get()));
        tryEditProviderWithTask(providerUUIDopt.get(), taskId);
      }
    }
  }

  protected Set<UUID> getProviderUUIDsToUse(UUID taskId, ITask task, ITaskParams params) {
    if (params instanceof UniverseTaskParams) {
      UUID universeUUID = ((UniverseTaskParams) params).universeUUID;
      Set<UUID> providerUUIDs = new HashSet<>();
      try {
        Universe universe = Universe.getOrBadRequest(universeUUID);
        for (UniverseDefinitionTaskParams.Cluster cluster :
            universe.getUniverseDetails().clusters) {
          providerUUIDs.add(UUID.fromString(cluster.userIntent.provider));
        }
      } catch (Exception e) {
        log.error("Cannot find universe for id " + universeUUID);
        return Collections.emptySet();
      }
      return providerUUIDs;
    }
    return Collections.emptySet();
  }

  protected Optional<UUID> getProviderUUIDToEdit(UUID taskId, ITask task, ITaskParams params) {
    if (params instanceof IProviderTaskParams) {
      return Optional.of(((IProviderTaskParams) params).getProviderUUID());
    }
    return Optional.empty();
  }

  public void onTaskFinished(UUID taskID) {
    if (!isEnabled()) {
      log.debug("Not enabled, skipping");
      return;
    }
    if (taskID == null) {
      return;
    }
    Collection<UUID> providerIds = providersByTask.remove(taskID);
    if (providerIds != null) {
      for (UUID providerUUID : providerIds) {
        doInProviderLock(
            providerUUID,
            () -> {
              if (!editTaskIdByProvider.remove(providerUUID, taskID)) {
                useTaskIdsByProvider.remove(providerUUID, taskID);
              }
            });
      }
    }
  }

  public void tryEditProvider(UUID providerUUID, Runnable action) {
    tryEditProvider(
        providerUUID,
        (Supplier<Void>)
            () -> {
              action.run();
              return null;
            });
  }

  public <V> V tryEditProvider(UUID providerUUID, Supplier<V> action) {
    if (!isEnabled()) {
      log.debug("Not enabled, skipping");
      return action.get();
    }
    AtomicReference<V> result = new AtomicReference<>();
    log.debug("Try to edit provider {}", providerUUID);
    doInProviderLock(
        providerUUID,
        () -> {
          verifyNotUnderEdit(providerUUID);
          verifyNotUsed(providerUUID);
          result.set(action.get());
        });
    return result.get();
  }

  private void tryEditProviderWithTask(UUID providerUUID, UUID taskID) {
    log.debug("Try to edit provider {} with task {}", providerUUID, taskID);
    doInProviderLock(
        providerUUID,
        () -> {
          verifyNotUnderEdit(providerUUID);
          verifyNotUsed(providerUUID);
          log.debug("Edit provider {} with task {} ", providerUUID, taskID);
          editTaskIdByProvider.put(providerUUID, taskID);
        });
  }

  private void tryUseProviderWithTask(UUID providerUUID, UUID taskID) {
    log.debug("Try to use provider {} with task {}", providerUUID, taskID);
    doInProviderLock(
        providerUUID,
        () -> {
          verifyNotUnderEdit(providerUUID);
          log.debug("Use provider {} with task {} ", providerUUID, taskID);
          useTaskIdsByProvider.put(providerUUID, taskID);
        });
  }

  private void verifyNotUsed(UUID providerUUID) {
    if (!useTaskIdsByProvider.get(providerUUID).isEmpty()) {
      throw new PlatformServiceException(
          Http.Status.SERVICE_UNAVAILABLE,
          "Provider " + providerUUID + " resources are currently in use");
    }
  }

  private void verifyNotUnderEdit(UUID providerUUID) {
    UUID editTaskUUID = editTaskIdByProvider.get(providerUUID);
    if (editTaskUUID != null) {
      throw new PlatformServiceException(
          Http.Status.SERVICE_UNAVAILABLE,
          "Provider " + providerUUID + " is currently edited by task " + editTaskUUID);
    }
  }

  private void garbageCollectTasks(UUID providerUUID) {
    editTaskIdByProvider.compute(
        providerUUID,
        (key, editTaskUUID) -> checkIfTaskIsActive(editTaskUUID) ? editTaskUUID : null);
    for (UUID taskUUID : new ArrayList<>(useTaskIdsByProvider.get(providerUUID))) {
      if (!checkIfTaskIsActive(taskUUID)) {
        useTaskIdsByProvider.remove(providerUUID, taskUUID);
      }
    }
  }

  private void doInProviderLock(UUID providerUUID, Runnable runnable) {
    log.debug(
        "Trying to do provider {} lock with timeout {}", providerUUID, getLockTimeoutMillis());
    Lock providerLock = getProviderLock(providerUUID);
    boolean locked = false;
    try {
      locked = providerLock.tryLock(getLockTimeoutMillis(), TimeUnit.MILLISECONDS);
    } catch (InterruptedException e) {
      log.warn("Interrupted while waiting for lock", e);
    }
    if (!locked) {
      throw new PlatformServiceException(
          Http.Status.SERVICE_UNAVAILABLE, "Unable to lock provider " + providerUUID);
    }
    try {
      log.debug("Locked {} provider", providerUUID);
      garbageCollectTasks(providerUUID);
      runnable.run();
    } finally {
      providerLock.unlock();
      log.debug("Released provider {} lock", providerUUID);
    }
  }

  private Lock getProviderLock(UUID providerUUID) {
    return providerLocks.computeIfAbsent(providerUUID, (pid) -> new ReentrantLock());
  }

  private boolean checkIfTaskIsActive(UUID taskUUID) {
    if (taskUUID == null) {
      return false;
    }
    TaskInfo taskInfo = TaskInfo.get(taskUUID); // Don't need exception if no task.
    return taskInfo != null && ACTIVE_STATES.contains(taskInfo.getTaskState());
  }

  protected boolean isEnabled() {
    return runtimeConfigFactory.globalRuntimeConf().getBoolean(EDIT_PROVIDER_NEW_ENABLED_KEY);
  }

  protected long getLockTimeoutMillis() {
    return PROVIDER_LOCK_TIMEOUT_MILLIS;
  }
}
