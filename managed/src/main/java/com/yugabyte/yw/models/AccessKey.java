// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonBackReference;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import play.data.validation.Constraints;

import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import java.util.List;
import java.util.UUID;

@Entity
public class AccessKey extends Model {
  public static class KeyInfo {
    public String publicKey;
    public String privateKey;
    public String vaultPasswordFile;
    public String vaultFile;
    public String sshUser;
    public Integer sshPort;
    public boolean airGapInstall = false;
    public boolean passwordlessSudoAccess = true;
    public String provisionInstanceScript = "";
    public boolean installNodeExporter = true;
    public Integer nodeExporterPort = 9300;
    public String nodeExporterUser = "prometheus";
    public boolean skipProvisioning = false;
  }

  @EmbeddedId
  @Constraints.Required
  public AccessKeyId idKey;

  @JsonBackReference
  public String getKeyCode() { return this.idKey.keyCode; }
  @JsonBackReference
  public UUID getProviderUUID() { return this.idKey.providerUUID; }

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  private KeyInfo keyInfo;

  public void setKeyInfo(KeyInfo info) { this.keyInfo = info; }
  public KeyInfo getKeyInfo() { return this.keyInfo; }

  public static AccessKey create(UUID providerUUID, String keyCode, KeyInfo keyInfo) {
    AccessKey accessKey = new AccessKey();
    accessKey.idKey = AccessKeyId.create(providerUUID, keyCode);
    accessKey.setKeyInfo(keyInfo);
    accessKey.save();
    return accessKey;
  }

  private static final Finder<AccessKeyId, AccessKey> find =
    new Finder<AccessKeyId, AccessKey>(AccessKey.class) {};

  public static AccessKey get(AccessKeyId accessKeyId) {
    return find.byId(accessKeyId);
  }

  public static AccessKey get(UUID providerUUID, String keyCode) {
    return find.byId(AccessKeyId.create(providerUUID, keyCode));
  }

  public static List<AccessKey> getAll(UUID providerUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).findList();
  }

  public static List<AccessKey> getAll() {
    return find.query().findList();
  }
}
