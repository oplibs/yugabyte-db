/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import akka.actor.ActorSystem;
import akka.actor.Cancellable;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import java.io.File;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import scala.concurrent.ExecutionContext;

@Singleton
public class PlatformReplicationManager {
  private static final String BACKUP_SCRIPT = "bin/yb_platform_backup.sh";
  static final String DB_PASSWORD_ENV_VAR_KEY = "PGPASSWORD";

  private final AtomicReference<Cancellable> schedule;

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final PlatformReplicationHelper replicationHelper;

  private static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationManager.class);

  @Inject
  public PlatformReplicationManager(
      ActorSystem actorSystem,
      ExecutionContext executionContext,
      PlatformReplicationHelper replicationHelper) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.replicationHelper = replicationHelper;
    this.schedule = new AtomicReference<>(null);
  }

  private Cancellable getSchedule() {
    return this.schedule.get();
  }

  public void start() {
    if (replicationHelper.isBackupScheduleRunning(this.getSchedule())) {
      LOG.warn("Platform backup schedule is already started");
      return;
    }

    if (!replicationHelper.isBackupScheduleEnabled()) {
      LOG.debug("Cannot start backup schedule because it is disabled");
      return;
    }

    Duration frequency = replicationHelper.getBackupFrequency();

    if (!frequency.isNegative() && !frequency.isZero()) {
      this.schedule.set(this.createSchedule(frequency));
    }
  }

  public void stop() {
    if (!replicationHelper.isBackupScheduleRunning(this.getSchedule())) {
      LOG.debug("Platform backup schedule is already stopped");
      return;
    }

    if (!this.getSchedule().cancel()) {
      LOG.warn("Unknown error occurred stopping platform backup schedule");
    }
  }

  public void init() {
    // Start periodic platform sync schedule if enabled.
    this.start();
    // Switch prometheus to federated if this platform is a follower for HA.
    replicationHelper.ensurePrometheusConfig();
  }

  public JsonNode stopAndDisable() {
    this.stop();
    replicationHelper.setBackupScheduleEnabled(false);

    return this.getBackupInfo();
  }

  public JsonNode setFrequencyStartAndEnable(Duration duration) {
    this.stop();
    replicationHelper.setReplicationFrequency(duration);
    replicationHelper.setBackupScheduleEnabled(true);
    this.start();
    return this.getBackupInfo();
  }

  private Cancellable createSchedule(Duration frequency) {
    LOG.info("Scheduling periodic platform backups every {}", frequency.toString());
    return this.actorSystem
        .scheduler()
        .schedule(
            Duration.ofMillis(0), // initialDelay
            frequency, // interval
            this::sync,
            this.executionContext);
  }

  public List<File> listBackups(URL leader) {
    return replicationHelper.listBackups(leader);
  }

  public JsonNode getBackupInfo() {
    return replicationHelper.getBackupInfoJson(
        replicationHelper.getBackupFrequency().toMillis(),
        replicationHelper.isBackupScheduleRunning(this.getSchedule()));
  }

  public void demoteLocalInstance(PlatformInstance localInstance, String leaderAddr)
      throws MalformedURLException {
    if (!localInstance.getIsLocal()) {
      throw new RuntimeException("Cannot perform this action on a remote instance");
    }

    // Stop the old backup schedule.
    this.stopAndDisable();

    // Demote the local instance to follower.
    localInstance.demote();

    // Try switching local prometheus to read from the reported leader.
    replicationHelper.switchPrometheusToFederated(new URL(leaderAddr));
  }

  public void promoteLocalInstance(PlatformInstance newLeader) {
    HighAvailabilityConfig config = newLeader.getConfig();
    Optional<PlatformInstance> previousLocal = config.getLocal();

    if (!previousLocal.isPresent()) {
      throw new RuntimeException("No local instance associated with backup being restored");
    }

    // Update which instance should be local.
    previousLocal.get().setIsLocalAndUpdate(false);
    config
        .getInstances()
        .forEach(
            i -> {
              i.setIsLocalAndUpdate(i.getUUID().equals(newLeader.getUUID()));
              try {
                // Clear out any old backups.
                replicationHelper.cleanupReceivedBackups(new URL(i.getAddress()), 0);
              } catch (MalformedURLException ignored) {
              }
            });

    // Mark the failover timestamp.
    config.updateLastFailover();
    // Attempt to ensure all remote instances are in follower state.
    // Remotely demote any instance reporting to be a leader.
    config.getRemoteInstances().forEach(replicationHelper::demoteRemoteInstance);
    // Promote the new local leader.
    newLeader.promote();
  }

  /**
   * A method to import a list of platform instances received from the leader platform instance.
   * Assumption is that any platform instance existing locally but not provided in the payload has
   * been deleted on the leader, and thus should be deleted here too.
   *
   * @param config the local HA Config model
   * @param instancesJson the JSON payload received from the leader instance
   */
  public Set<PlatformInstance> importPlatformInstances(
      HighAvailabilityConfig config, ArrayNode instancesJson) {
    List<PlatformInstance> existingInstances = config.getInstances();
    // Get list of existing addresses.
    Set<String> existingAddrs =
        existingInstances.stream().map(PlatformInstance::getAddress).collect(Collectors.toSet());

    // Map request JSON payload to list of platform instances.
    Set<PlatformInstance> newInstances =
        StreamSupport.stream(instancesJson.spliterator(), false)
            .map(obj -> Json.fromJson(obj, PlatformInstance.class))
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());

    // Get list of request payload addresses.
    Set<String> newAddrs =
        newInstances.stream().map(PlatformInstance::getAddress).collect(Collectors.toSet());

    // Delete any instances that exist locally but aren't included in the sync request.
    Set<String> instanceAddrsToDelete = Sets.difference(existingAddrs, newAddrs);
    existingInstances
        .stream()
        .filter(i -> instanceAddrsToDelete.contains(i.getAddress()))
        .forEach(PlatformInstance::delete);

    // Import the new instances, or update existing ones.
    return newInstances
        .stream()
        .map(replicationHelper::processImportedInstance)
        .filter(Optional::isPresent)
        .map(Optional::get)
        .collect(Collectors.toSet());
  }

  @VisibleForTesting
  boolean sendBackup(PlatformInstance remoteInstance) {
    HighAvailabilityConfig config = remoteInstance.getConfig();
    String clusterKey = config.getClusterKey();
    boolean result =
        replicationHelper
            .getMostRecentBackup()
            .map(
                backup ->
                    replicationHelper.exportBackups(
                            config, clusterKey, remoteInstance.getAddress(), backup)
                        && remoteInstance.updateLastBackup())
            .orElse(false);
    if (!result) {
      LOG.error("Error sending platform backup to " + remoteInstance.getAddress());
    }

    return result;
  }

  public void oneOffSync() {
    if (replicationHelper.isBackupScheduleEnabled()) {
      this.sync();
    }
  }

  private synchronized void sync() {
    HighAvailabilityConfig.get()
        .ifPresent(
            config -> {
              try {
                List<PlatformInstance> remoteInstances = config.getRemoteInstances();
                // No point in taking a backup if there is no one to send it to.
                if (remoteInstances.isEmpty()) {
                  LOG.debug("Skipping HA cluster sync...");

                  return;
                }

                // Create the platform backup.
                if (!this.createBackup()) {
                  LOG.error("Error creating platform backup");

                  return;
                }

                // Update local last backup time if creating the backup succeeded.
                config
                    .getLocal()
                    .ifPresent(
                        localInstance -> {
                          localInstance.updateLastBackup();

                          // Send the platform backup to all followers.
                          Set<PlatformInstance> instancesToSync =
                              remoteInstances
                                  .stream()
                                  .filter(this::sendBackup)
                                  .collect(Collectors.toSet());

                          // Sync the HA cluster state to all followers that successfully received a
                          // backup.
                          instancesToSync.forEach(replicationHelper::syncToRemoteInstance);
                        });
              } catch (Exception e) {
                LOG.error("Error running sync for HA config {}", config.getUUID(), e);
              } finally {
                // Remove locally created backups since they have already been sent to followers.
                replicationHelper.cleanupCreatedBackups();
              }
            });
  }

  public void cleanupReceivedBackups(URL leader) {
    replicationHelper.cleanupReceivedBackups(leader, replicationHelper.getNumBackupsRetention());
  }

  public boolean saveReplicationData(String fileName, File uploadedFile, URL leader, URL sender) {
    Path replicationDir = replicationHelper.getReplicationDirFor(leader.getHost());
    Path saveAsFile = Paths.get(replicationDir.toString(), fileName);
    if (replicationDir.toFile().exists() || replicationDir.toFile().mkdirs()) {
      try {
        Util.moveFile(uploadedFile.toPath(), saveAsFile);
        LOG.debug(
            "Store platform backup received from leader {} via {} as {}.",
            leader.toString(),
            sender.toString(),
            saveAsFile);

        return true;
      } catch (IOException ioException) {
        LOG.error("File move failed from {} as {}", uploadedFile.toPath(), saveAsFile, ioException);
      }
    } else {
      LOG.error(
          "Could create folder {} to store platform backup received from leader {} via {}",
          replicationDir,
          leader.toString(),
          sender.toString());
    }

    return false;
  }

  public void switchPrometheusToStandalone() {
    this.replicationHelper.switchPrometheusToStandalone();
  }

  abstract class PlatformBackupParams {
    // The addr that the prometheus server is running on.
    private final String prometheusHost;
    // The username that YW uses to connect to it's DB.
    private final String dbUsername;
    // The password that YW uses to authenticate connections to it's DB.
    private final String dbPassword;
    // The addr that the DB is listening to connection requests on.
    private final String dbHost;
    // The port that the DB is listening to connection requests on.
    private final int dbPort;

    protected PlatformBackupParams() {
      this.prometheusHost = replicationHelper.getPrometheusHost();
      this.dbUsername = replicationHelper.getDBUser();
      this.dbPassword = replicationHelper.getDBPassword();
      this.dbHost = replicationHelper.getDBHost();
      this.dbPort = replicationHelper.getDBPort();
    }

    protected abstract List<String> getCommandSpecificArgs();

    List<String> getCommandArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add(BACKUP_SCRIPT);
      commandArgs.addAll(getCommandSpecificArgs());
      commandArgs.add("--db_username");
      commandArgs.add(dbUsername);
      commandArgs.add("--db_host");
      commandArgs.add(dbHost);
      commandArgs.add("--db_port");
      commandArgs.add(Integer.toString(dbPort));
      commandArgs.add("--prometheus_host");
      commandArgs.add(prometheusHost);
      commandArgs.add("--verbose");
      commandArgs.add("--skip_restart");

      return commandArgs;
    }

    Map<String, String> getExtraVars() {
      Map<String, String> extraVars = new HashMap<>();

      if (dbPassword != null && !dbPassword.isEmpty()) {
        // Add PGPASSWORD env var to skip having to enter the db password for pg_dump/pg_restore.
        extraVars.put(DB_PASSWORD_ENV_VAR_KEY, dbPassword);
      }

      return extraVars;
    }
  }

  private class CreatePlatformBackupParams extends PlatformBackupParams {
    // Whether to exclude prometheus metric data from the backup or not.
    private final boolean excludePrometheus;
    // Whether to exclude the YB release binaries from the backup or not.
    private final boolean excludeReleases;
    // Where to output the platform backup
    private final String outputDirectory;

    CreatePlatformBackupParams() {
      this.excludePrometheus = true;
      this.excludeReleases = true;
      this.outputDirectory = replicationHelper.getBackupDir().toString();
    }

    @Override
    protected List<String> getCommandSpecificArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add("create");

      if (excludePrometheus) {
        commandArgs.add("--exclude_prometheus");
      }

      if (excludeReleases) {
        commandArgs.add("--exclude_releases");
      }

      commandArgs.add("--output");
      commandArgs.add(outputDirectory);

      return commandArgs;
    }
  }

  private class RestorePlatformBackupParams extends PlatformBackupParams {
    // Where to input a previously taken platform backup from.
    private final File input;

    RestorePlatformBackupParams(File input) {
      this.input = input;
    }

    @Override
    protected List<String> getCommandSpecificArgs() {
      List<String> commandArgs = new ArrayList<>();
      commandArgs.add("restore");
      commandArgs.add("--input");
      commandArgs.add(input.getAbsolutePath());

      return commandArgs;
    }
  }

  /**
   * Create a backup of the Yugabyte Platform
   *
   * @return the output/results of running the script
   */
  @VisibleForTesting
  boolean createBackup() {
    LOG.debug("Creating platform backup...");

    ShellResponse response = replicationHelper.runCommand(new CreatePlatformBackupParams());

    if (response.code != 0) {
      LOG.error("Backup failed: " + response.message);
    }

    return response.code == 0;
  }

  /**
   * Restore a backup of the Yugabyte Platform
   *
   * @param input is the path to the backup to be restored
   * @return the output/results of running the script
   */
  public boolean restoreBackup(File input) {
    LOG.info("Restoring platform backup...");

    ShellResponse response = replicationHelper.runCommand(new RestorePlatformBackupParams(input));
    if (response.code != 0) {
      LOG.error("Restore failed: " + response.message);
    }

    return response.code == 0;
  }
}
