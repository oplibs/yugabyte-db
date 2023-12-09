package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.TableType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.persistence.CascadeType;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Entity
@ApiModel(description = "disaster recovery config object")
@Getter
@Setter
public class DrConfig extends Model {

  private static final Finder<UUID, DrConfig> find = new Finder<>(DrConfig.class) {};
  private static final Finder<UUID, XClusterConfig> findXClusterConfig =
      new Finder<>(XClusterConfig.class) {};

  @Id
  @ApiModelProperty(value = "DR config UUID")
  private UUID uuid;

  @ApiModelProperty(value = "Disaster recovery config name")
  private String name;

  @ApiModelProperty(value = "Create time of the DR config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createTime;

  @ApiModelProperty(value = "Last modify time of the DR config", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date modifyTime;

  @OneToMany(mappedBy = "drConfig", cascade = CascadeType.ALL)
  @JsonIgnore
  private List<XClusterConfig> xClusterConfigs;

  @Transactional
  public static DrConfig create(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID, Set<String> tableIds) {
    DrConfig drConfig = new DrConfig();
    drConfig.name = name;
    drConfig.setCreateTime(new Date());
    drConfig.setModifyTime(new Date());

    // Create a corresponding xCluster object.
    String xClusterConfigName =
        drConfig.getNewXClusterConfigName(sourceUniverseUUID, targetUniverseUUID);
    // Set imported to true to use the new replication group name convention.
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            xClusterConfigName,
            sourceUniverseUUID,
            targetUniverseUUID,
            XClusterConfigStatusType.Initialized,
            true /* imported */);
    xClusterConfig.setDrConfig(drConfig);
    xClusterConfig.update();
    drConfig.xClusterConfigs.add(xClusterConfig);
    // Dr only supports ysql tables.
    xClusterConfig.setTableType(TableType.YSQL);
    // Dr is only based on transactional replication.
    xClusterConfig.setType(ConfigType.Txn);
    xClusterConfig.updateTables(tableIds, tableIds /* tableIdsNeedBootstrap */);

    drConfig.save();
    return drConfig;
  }

  @JsonProperty("xClusterConfig")
  public XClusterConfig getActiveXClusterConfig() {
    if (xClusterConfigs.isEmpty()) {
      throw new IllegalStateException(
          String.format(
              "DrConfig %s(%s) does not have any corresponding xCluster config",
              this.name, this.uuid));
    }
    // For now just return the first element. For later expansion, a dr config can handle several
    // xCluster configs.
    return xClusterConfigs.get(0);
  }

  private String getNewXClusterConfigName(UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    int id = this.xClusterConfigs.size();
    String newName = "__DR_CONFIG_" + this.name + "_" + id;
    XClusterConfigTaskBase.checkConfigDoesNotAlreadyExist(
        newName, sourceUniverseUUID, targetUniverseUUID);
    return newName;
  }

  public static DrConfig getValidConfigOrBadRequest(Customer customer, UUID drConfigUuid) {
    DrConfig drConfig = getOrBadRequest(drConfigUuid);
    drConfig.xClusterConfigs.forEach(
        xClusterConfig -> XClusterConfig.checkXClusterConfigInCustomer(xClusterConfig, customer));
    return drConfig;
  }

  public static DrConfig getOrBadRequest(UUID drConfigUuid) {
    return maybeGet(drConfigUuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Cannot find drConfig with uuid " + drConfigUuid));
  }

  public static Optional<DrConfig> maybeGet(UUID drConfigUuid) {
    DrConfig drConfig =
        find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne();
    if (drConfig == null) {
      log.info("Cannot find drConfig {} with uuid ", drConfig);
      return Optional.empty();
    }
    return Optional.of(drConfig);
  }

  public static Optional<DrConfig> maybeGetByName(String drConfigName) {
    DrConfig drConfig =
        find.query().fetch("xClusterConfigs", "").where().eq("name", drConfigName).findOne();
    if (drConfig == null) {
      log.info("Cannot find drConfig {} with uuid ", drConfig);
      return Optional.empty();
    }
    return Optional.of(drConfig);
  }

  public static List<DrConfig> getBySourceUniverseUuid(UUID sourceUniverseUuid) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBySourceUniverseUUID(sourceUniverseUuid);
    Set<UUID> drConfigUuidList =
        xClusterConfigs.stream()
            .filter(XClusterConfig::isUsedForDr)
            .map(xClusterConfig -> xClusterConfig.getDrConfig().getUuid())
            .collect(Collectors.toSet());
    List<DrConfig> drConfigs = new ArrayList<>();
    drConfigUuidList.forEach(
        drConfigUuid ->
            drConfigs.add(
                find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne()));
    return drConfigs;
  }

  public static List<DrConfig> getByTargetUniverseUuid(UUID targetUniverseUuid) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByTargetUniverseUUID(targetUniverseUuid);
    Set<UUID> drConfigUuidList =
        xClusterConfigs.stream()
            .filter(XClusterConfig::isUsedForDr)
            .map(xClusterConfig -> xClusterConfig.getDrConfig().getUuid())
            .collect(Collectors.toSet());
    List<DrConfig> drConfigs = new ArrayList<>();
    drConfigUuidList.forEach(
        drConfigUuid ->
            drConfigs.add(
                find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne()));
    return drConfigs;
  }

  public static List<DrConfig> getByUniverseUuid(UUID universeUuid) {
    return Stream.concat(
            getBySourceUniverseUuid(universeUuid).stream(),
            getByTargetUniverseUuid(universeUuid).stream())
        .collect(Collectors.toList());
  }

  public static List<DrConfig> getBetweenUniverses(
      UUID sourceUniverseUuid, UUID targetUniverseUuid) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBetweenUniverses(sourceUniverseUuid, targetUniverseUuid);
    Set<UUID> drConfigUuidList =
        xClusterConfigs.stream()
            .filter(XClusterConfig::isUsedForDr)
            .map(xClusterConfig -> xClusterConfig.getDrConfig().getUuid())
            .collect(Collectors.toSet());
    List<DrConfig> drConfigs = new ArrayList<>();
    drConfigUuidList.forEach(
        drConfigUuid ->
            drConfigs.add(
                find.query().fetch("xClusterConfigs").where().eq("uuid", drConfigUuid).findOne()));
    return drConfigs;
  }
}
