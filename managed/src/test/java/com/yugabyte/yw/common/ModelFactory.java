// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import play.libs.Json;

import java.util.HashSet;
import java.util.Map;
import java.util.UUID;

public class ModelFactory {

  /*
   * Customer creation helpers.
   */

  public static Customer testCustomer() {
    return testCustomer("test@customer.com");
  }
  public static Customer testCustomer(String email) {
    return testCustomer("tc", email);
  }
  public static Customer testCustomer(String code, String email) {
    return Customer.create(code, "Test customer", email, "password");
  }

  /*
   * Provider creation helpers.
   */

  public static Provider awsProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.aws, "Amazon");
  }
  public static Provider gcpProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.gcp, "Google");
  }
  public static Provider onpremProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.onprem, "OnPrem");
  }
  public static Provider newProvider(Customer customer, Common.CloudType cloud) {
    return Provider.create(customer.uuid, cloud, cloud.toString());
  }
  public static Provider newProvider(Customer customer, Common.CloudType cloud, Map<String, String> config) {
    return Provider.create(customer.uuid, cloud, cloud.toString(), config);
  }

  /*
   * Universe creation helpers.
   */

  public static Universe createUniverse() {
    return createUniverse("Test Universe", 1L);
  }
  public static Universe createUniverse(String universeName) {
    return createUniverse(universeName, 1L);
  }
  public static Universe createUniverse(long customerId) {
    return createUniverse("Test Universe", customerId);
  }
  public static Universe createUniverse(String universeName, long customerId) {
    return createUniverse(universeName, UUID.randomUUID(), customerId);
  }
  public static Universe createUniverse(String universeName, UUID universeUUID) {
    return createUniverse(universeName, universeUUID, 1L);
  }
  public static Universe createUniverse(String universeName, UUID universeUUID, long customerId) {
    UniverseDefinitionTaskParams.UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.universeName = universeName;
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = universeUUID;
    params.nodeDetailsSet = new HashSet<>();
    params.upsertPrimaryCluster(userIntent, null);
    return Universe.create(params, customerId);
  }

  public static CustomerConfig createS3StorageConfig(Customer customer) {
    JsonNode formData = Json.parse("{\"name\": \"S3\", \"type\": \"STORAGE\", \"data\": " +
        "{\"S3_BUCKET\": \"s3://foo\", \"ACCESS_KEY\": \"A-KEY\", \"ACCESS_SECRET\": \"A-SECRET\"}}");
    return CustomerConfig.createWithFormData(customer.uuid, formData);
  }
}
