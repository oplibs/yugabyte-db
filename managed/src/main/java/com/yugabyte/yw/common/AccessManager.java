// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.UUID;

@Singleton
public class AccessManager extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(AccessManager.class);

  @Inject
  play.Configuration appConfig;

  private static final String YB_CLOUD_COMMAND_TYPE = "access";
  private static final String PEM_PERMISSIONS = "r--------";
  private static final String PUB_PERMISSIONS = "rw-r--r--";

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  public enum KeyType {
    PUBLIC,
    PRIVATE;

    public String getExtension() {
      switch(this) {
        case PUBLIC:
          return ".pub";
        case PRIVATE:
          return ".pem";
        default:
          return null;
      }
    }
  }

  private String getOrCreateKeyFilePath(UUID providerUUID) {
    File keyBasePathName = new File(appConfig.getString("yb.storage.path"), "/keys");
    if (!keyBasePathName.exists() && !keyBasePathName.mkdirs()) {
      throw new RuntimeException("Key path " +
          keyBasePathName.getAbsolutePath() + " doesn't exists.");
    }

    File keyFilePath = new File(keyBasePathName.getAbsoluteFile(), providerUUID.toString());
    if (keyFilePath.isDirectory() || keyFilePath.mkdirs()) {
      return keyFilePath.getAbsolutePath();
    }

    throw new RuntimeException("Unable to create key file path " + keyFilePath.getAbsolutePath());
  }

  // This method would upload the provided key file to the provider key file path.
  public AccessKey uploadKeyFile(UUID regionUUID, File uploadedFile,
                                 String keyCode, KeyType keyType, String sshUser) {
    Region region = Region.get(regionUUID);
    String keyFilePath = getOrCreateKeyFilePath(region.provider.uuid);
    AccessKey accessKey = AccessKey.get(region.provider.uuid, keyCode);
    if (accessKey != null) {
      throw new RuntimeException("Duplicate Access KeyCode: " + keyCode);
    }
    Path source = Paths.get(uploadedFile.getAbsolutePath());
    Path destination = Paths.get(keyFilePath, keyCode + keyType.getExtension());
    if (!Files.exists(source)) {
      throw new RuntimeException("Key file " + source.getFileName() + " not found.");
    }
    if (Files.exists(destination) ) {
      throw new RuntimeException("File " + destination.getFileName() + " already exists.");
    }

    try {
      Files.move(source, destination);
      Set<PosixFilePermission> permissions = PosixFilePermissions.fromString(PEM_PERMISSIONS);
      if (keyType == AccessManager.KeyType.PUBLIC) {
        permissions = PosixFilePermissions.fromString(PUB_PERMISSIONS);
      }
      Files.setPosixFilePermissions(destination, permissions);
    } catch (IOException e) {
      LOG.error(e.getMessage(), e);
      throw new RuntimeException("Unable to upload key file " + source.getFileName());
    }

    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    if (keyType == AccessManager.KeyType.PUBLIC) {
      keyInfo.publicKey = destination.toAbsolutePath().toString();
    } else {
      keyInfo.privateKey = destination.toAbsolutePath().toString();
    }
    JsonNode vaultResponse = createVault(regionUUID, keyInfo.privateKey);
    if (vaultResponse.has("error")) {
      throw new RuntimeException(vaultResponse.get("error").asText());
    }
    keyInfo.vaultFile = vaultResponse.get("vault_file").asText();
    keyInfo.vaultPasswordFile = vaultResponse.get("vault_password").asText();
    keyInfo.sshUser = sshUser;
    return AccessKey.create(region.provider.uuid, keyCode, keyInfo);
  }

  // This method would create a public/private key file and upload that to
  // the provider cloud account. And store the credentials file in the keyFilePath
  // and return the file names. It will also create the vault file
  public AccessKey addKey(UUID regionUUID, String keyCode) {
    List<String> commandArgs = new ArrayList<String>();
    Region region = Region.get(regionUUID);
    String keyFilePath = getOrCreateKeyFilePath(region.provider.uuid);
    AccessKey accessKey = AccessKey.get(region.provider.uuid, keyCode);

    commandArgs.add("--key_pair_name");
    commandArgs.add(keyCode);
    commandArgs.add("--key_file_path");
    commandArgs.add(keyFilePath);

    if (accessKey != null && accessKey.getKeyInfo().privateKey != null) {
      commandArgs.add("--private_key_file");
      commandArgs.add(accessKey.getKeyInfo().privateKey);
    }

    JsonNode response = execAndParseCommandRegion(regionUUID, "add-key", commandArgs);
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }

    if (accessKey == null) {
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.publicKey = response.get("public_key").asText();
      keyInfo.privateKey = response.get("private_key").asText();
      JsonNode vaultResponse = createVault(regionUUID, keyInfo.privateKey);
      if (response.has("error")) {
        throw new RuntimeException(response.get("error").asText());
      }
      keyInfo.vaultFile = vaultResponse.get("vault_file").asText();
      keyInfo.vaultPasswordFile = vaultResponse.get("vault_password").asText();
      accessKey = AccessKey.create(region.provider.uuid, keyCode, keyInfo);
    }
    return accessKey;
  }

  public JsonNode createVault(UUID regionUUID, String privateKeyFile) {
    List<String> commandArgs = new ArrayList<String>();

    if (!new File(privateKeyFile).exists()) {
      throw new RuntimeException("File " + privateKeyFile + " doesn't exists.");
    }
    commandArgs.add("--private_key_file");
    commandArgs.add(privateKeyFile);
    return execAndParseCommandRegion(regionUUID, "create-vault", commandArgs);
  }

  public JsonNode listKeys(UUID regionUUID) {
    return execAndParseCommandRegion(regionUUID, "list-keys", Collections.emptyList());
  }

  public JsonNode deleteKey(UUID regionUUID, String keyCode) {
    List<String> commandArgs = new ArrayList<String>();
    Region region = Region.get(regionUUID);
    if (region == null) {
      throw new RuntimeException("Invalid Region UUID: " + regionUUID);
    }
    String keyFilePath = getOrCreateKeyFilePath(region.provider.uuid);

    commandArgs.add("--key_pair_name");
    commandArgs.add(keyCode);
    commandArgs.add("--key_file_path");
    commandArgs.add(keyFilePath);
    JsonNode response = execAndParseCommandRegion(regionUUID, "delete-key", commandArgs);
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }
    return response;
  }
  
  public String createCredentialsFile(UUID providerUUID, JsonNode credentials) 
  		throws IOException {
  	ObjectMapper mapper = new ObjectMapper();
  	String credentialsFilePath = getOrCreateKeyFilePath(providerUUID)+"/credentials.json";
  	mapper.writeValue(new File(credentialsFilePath), credentials);
  	return credentialsFilePath;
  }
}

