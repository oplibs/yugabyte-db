/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import java.lang.reflect.Constructor;
import java.util.Base64;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;
import play.api.Play;
import play.libs.Json;

public class EncryptionAtRestUtil {
    protected static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestUtil.class);

    public static Constructor getConstructor(Class serviceClass) {
        Constructor serviceConstructor = null;
        Class[] parameterTypes;
        for (Constructor constructor : serviceClass.getConstructors()) {
            parameterTypes = constructor.getParameterTypes();
            if (constructor.getParameterCount() == 0) {
                serviceConstructor = constructor;
                break;
            }
        }
        return serviceConstructor;
    }

    public static ObjectNode getAuthConfig(UUID configUUID, KeyProvider keyProvider) {
        KmsConfig config = KmsConfig.get(configUUID);
        final ObjectNode maskedConfig = (ObjectNode) config.authConfig;
        UUID customerUUID = config.customerUUID;
        return (ObjectNode) EncryptionAtRestUtil
                .unmaskConfigData(customerUUID, maskedConfig, keyProvider);
    }

    public static <N extends JsonNode> ObjectNode maskConfigData(
            UUID customerUUID,
            N config,
            KeyProvider keyProvider
    ) {
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String salt = generateSalt(customerUUID, keyProvider);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String encryptedConfig = encryptor.encrypt(mapper.writeValueAsString(config));
            return Json.newObject().put("encrypted", encryptedConfig);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not encrypt %s KMS configuration for customer %s",
                    keyProvider.name(),
                    customerUUID.toString()
            );
            LOG.error(errMsg, e);
            return null;
        }
    }

    public static JsonNode unmaskConfigData(
            UUID customerUUID,
            ObjectNode config,
            KeyProvider keyProvider
    ) {
        if (config == null) return null;
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String encryptedConfig = config.get("encrypted").asText();
            final String salt = generateSalt(customerUUID, keyProvider);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String decryptedConfig = encryptor.decrypt(encryptedConfig);
            return mapper.readValue(decryptedConfig, JsonNode.class);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not decrypt %s KMS configuration for customer %s",
                    keyProvider.name(),
                    customerUUID.toString()
            );
            LOG.error(errMsg, e);
            return null;
        }
    }

    public static String generateSalt(UUID customerUUID, KeyProvider keyProvider) {
        final String saltBase = "%s%s";
        final String salt = String.format(
                saltBase,
                customerUUID.toString().replace("-", ""),
                keyProvider.name().hashCode()
        );
        return salt.length() % 2 == 0 ? salt : salt + "0";
    }

    public static byte[] getUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef) {
        LOG.debug(String.format(
                "Retrieving universe key cache entry for universe %s and keyRef %s",
                universeUUID.toString(),
                Base64.getEncoder().encodeToString(keyRef)
        ));
        return Play.current().injector().instanceOf(EncryptionAtRestUniverseKeyCache.class)
                .getCacheEntry(universeUUID, keyRef);
    }

    public static void setUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef, byte[] keyVal) {
        LOG.debug(String.format(
                "Setting universe key cache entry for universe %s and keyRef %s",
                universeUUID.toString(),
                Base64.getEncoder().encodeToString(keyRef)
        ));
        Play.current().injector().instanceOf(EncryptionAtRestUniverseKeyCache.class)
                .setCacheEntry(universeUUID, keyRef, keyVal);
    }

    public static void removeUniverseKeyCacheEntry(UUID universeUUID) {
        LOG.debug(String.format(
                "Removing universe key cache entry for universe %s",
                universeUUID.toString()
        ));
        Play.current().injector().instanceOf(EncryptionAtRestUniverseKeyCache.class)
                .removeCacheEntry(universeUUID);
    }

    public static void addKeyRef(UUID universeUUID, UUID configUUID, byte[] ref) {
        KmsHistory.createKmsHistory(
                configUUID,
                universeUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY,
                Base64.getEncoder().encodeToString(ref)
        );
    }

    public static KmsHistory getActiveKey(UUID universeUUID) {
        KmsHistory activeHistory = null;
        try {
            activeHistory = KmsHistory.getActiveHistory(
                    universeUUID,
                    KmsHistoryId.TargetType.UNIVERSE_KEY
            );
        } catch (Exception e) {
            final String errMsg = "Could not get key ref";
            LOG.error(errMsg, e);
        }
        return activeHistory;
    }

    public static KmsHistory getLatestConfigKey(UUID universeUUID, UUID configUUID) {
        KmsHistory latestHistory = null;
        try {
            latestHistory = KmsHistory.getLatestConfigHistory(
                    universeUUID,
                    configUUID,
                    KmsHistoryId.TargetType.UNIVERSE_KEY
            );
        } catch (Exception e) {
            final String errMsg = "Could not get key ref";
            LOG.error(errMsg, e);
        }
        return latestHistory;
    }

    public static void removeKeyRotationHistory(UUID universeUUID, UUID configUUID) {
        // Remove key ref history for the universe
        KmsHistory.deleteAllConfigTargetKeyRefs(
                configUUID,
                universeUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY
        );
        // Remove in-memory key ref -> key val cache entry, if it exists
        EncryptionAtRestUtil.removeUniverseKeyCacheEntry(universeUUID);
    }

    public static boolean configInUse(UUID configUUID) {
        return KmsHistory.configHasHistory(configUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
    }

    public static int getNumKeyRotations(UUID universeUUID) {
        return getNumKeyRotations(universeUUID, null);
    }

    public static int getNumKeyRotations(UUID universeUUID, UUID configUUID) {
        int numRotations = 0;
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            List<KmsHistory> keyRotations = configUUID == null ?
                    KmsHistory.getAllTargetKeyRefs(
                            universeUUID,
                            KmsHistoryId.TargetType.UNIVERSE_KEY
                    ) :
                    KmsHistory.getAllConfigTargetKeyRefs(
                            configUUID,
                            universeUUID,
                            KmsHistoryId.TargetType.UNIVERSE_KEY
                    );
            if (keyRotations != null) {
                numRotations = keyRotations.size();
            }
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the number of key rotations " +
                            "universe %s",
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return numRotations;
    }

    public static void activateKeyRef(UUID universeUUID, UUID configUUID, byte[] keyRef) {
        KmsHistory.activateKeyRef(
                universeUUID,
                configUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY,
                Base64.getEncoder().encodeToString(keyRef)
        );
    }
}
