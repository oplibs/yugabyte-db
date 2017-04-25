// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;


import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.core.IsNull.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertFalse;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.List;
import java.util.UUID;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.Provider;

import play.libs.Json;
import play.mvc.Result;

public class InstanceTypeControllerTest extends FakeDBApplication {
  Customer customer;
  Provider awsProvider;
  Provider onPremProvider;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    awsProvider = ModelFactory.awsProvider(customer);
    onPremProvider = ModelFactory.newProvider(customer, Common.CloudType.onprem);
  }

  private JsonNode doListInstanceTypesAndVerify(UUID providerUUID, int status) {
    Result result = FakeApiHelper.doRequest("GET", "/api/customers/" + customer.uuid
        + "/providers/" + providerUUID + "/instance_types");
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doCreateInstanceTypeAndVerify(UUID providerUUID, JsonNode bodyJson, int status) {
    Result result = FakeApiHelper.doRequestWithBody(
        "POST",
        "/api/customers/" + customer.uuid + "/providers/" + providerUUID + "/instance_types",
        bodyJson);

    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doGetInstanceTypeAndVerify(UUID providerUUID, String instanceTypeCode, int status) {
    Result result = FakeApiHelper.doRequest("GET", "/api/customers/" + customer.uuid
        + "/providers/" + providerUUID + "/instance_types/" + instanceTypeCode);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doDeleteInstanceTypeAndVerify(UUID providerUUID, String instanceTypeCode, int status) {
    Result result = FakeApiHelper.doRequest("DELETE", "/api/customers/" + customer.uuid
        + "/providers/" + providerUUID + "/instance_types/" + instanceTypeCode);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void testListInstanceTypeWithInvalidProviderUUID() {
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doListInstanceTypesAndVerify(randomUUID, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }

  @Test
  public void testListEmptyInstanceTypeWithValidProviderUUID() {
    JsonNode json = doListInstanceTypesAndVerify(awsProvider.uuid, OK);
    assertEquals(0, json.size());
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID() {
    InstanceType[] instanceTypes = new InstanceType[2];

    // Add first InstanceType.
    InstanceType.VolumeDetails i1VolDetails = new InstanceType.VolumeDetails();
    i1VolDetails.volumeSizeGB = 100;
    i1VolDetails.volumeType = InstanceType.VolumeType.EBS;
    InstanceTypeDetails i1InstanceDetails = new InstanceTypeDetails();
    i1InstanceDetails.volumeDetailsList.add(i1VolDetails);
    i1InstanceDetails.setDefaultMountPaths();
    instanceTypes[0] = InstanceType.upsert(awsProvider.code, "test-i1", 2, 10.5, i1InstanceDetails);

    // Add second InstanceType.
    InstanceType.VolumeDetails i2VolDetails = new InstanceType.VolumeDetails();
    i2VolDetails.volumeSizeGB = 80;
    i2VolDetails.volumeType = InstanceType.VolumeType.EBS;
    InstanceTypeDetails i2InstanceDetails = new InstanceTypeDetails();
    i2InstanceDetails.volumeDetailsList.add(i2VolDetails);
    i2InstanceDetails.setDefaultMountPaths();
    instanceTypes[1] = InstanceType.upsert(awsProvider.code, "test-i2", 3, 9.0, i2InstanceDetails);

    JsonNode json = doListInstanceTypesAndVerify(awsProvider.uuid, OK);
    assertEquals(2, json.size());

    for (int idx = 0; idx < json.size(); ++idx) {
      JsonNode instance = json.get(idx);
      assertValue(instance, "instanceTypeCode", instanceTypes[idx].getInstanceTypeCode());
      assertThat(instance.get("numCores").asInt(), allOf(notNullValue(), equalTo(instanceTypes[idx].numCores)));
      assertThat(instance.get("memSizeGB").asDouble(), allOf(notNullValue(), equalTo(instanceTypes[idx].memSizeGB)));
      VolumeDetails targetDetails = instanceTypes[idx].instanceTypeDetails.volumeDetailsList.get(0);
      JsonNode jsonDetails = instance.get("instanceTypeDetails").get("volumeDetailsList").get(0);
      assertThat(jsonDetails.get("volumeSizeGB").asInt(), allOf(notNullValue(), equalTo(targetDetails.volumeSizeGB)));
      assertValue(jsonDetails, "volumeType", targetDetails.volumeType.toString());
      assertValue(jsonDetails, "mountPath", targetDetails.mountPath);
    }
    List<String> expectedCodes = ImmutableList.of("test-i1", "test-i2");
    assertValues(json, "instanceTypeCode", expectedCodes);
  }

  @Test
  public void testCreateInstanceTypeWithInvalidProviderUUID() {
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    idKey.put("providerCode", "aws");
    instanceTypeJson.put("memSizeGB", 10.9);
    instanceTypeJson.put("volumeCount", 1);
    instanceTypeJson.put("numCores", 3);
    instanceTypeJson.set("idKey", idKey);
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doCreateInstanceTypeAndVerify(randomUUID, instanceTypeJson, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }

  @Test
  public void testCreateInstanceTypeWithInvalidParams() {
    JsonNode json = doCreateInstanceTypeAndVerify(awsProvider.uuid, Json.newObject(), BAD_REQUEST);
    assertErrorNodeValue(json, "idKey", "This field is required");
    assertErrorNodeValue(json, "memSizeGB", "This field is required");
    assertErrorNodeValue(json, "numCores", "This field is required");
  }

  @Test
  public void testCreateInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.EBS;
    volumeDetails.volumeSizeGB = 10;
    details.volumeDetailsList.add(volumeDetails);
    details.setDefaultMountPaths();
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    idKey.put("providerCode", "aws");
    instanceTypeJson.set("idKey", idKey);
    instanceTypeJson.put("memSizeGB", 10.9);
    instanceTypeJson.put("numCores", 3);
    instanceTypeJson.set("instanceTypeDetails", Json.toJson(details));
    JsonNode json = doCreateInstanceTypeAndVerify(awsProvider.uuid, instanceTypeJson, OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "memSizeGB", "10.9");
    assertValue(json, "numCores", "3");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "10");
    assertValue(machineDetailsNode, "volumeType", "EBS");
    assertValue(machineDetailsNode, "mountPath", "/mnt/d0");
  }

  @Test
  public void testGetOnPremInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 20;
    volumeDetails.mountPath = "/tmp/path/";
    details.volumeDetailsList.add(volumeDetails);
    InstanceType it = InstanceType.upsert(onPremProvider.code, "test-i1", 3, 5.0, details);
    JsonNode json = doGetInstanceTypeAndVerify(onPremProvider.uuid, it.getInstanceTypeCode(), OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "memSizeGB", "5.0");
    assertValue(json, "numCores", "3");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "20");
    assertValue(machineDetailsNode, "volumeType", "SSD");
    assertValue(machineDetailsNode, "mountPath", "/tmp/path/");
  }

  @Test
  public void testGetInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 20;
    details.volumeDetailsList.add(volumeDetails);
    details.volumeDetailsList.add(volumeDetails);
    InstanceType it = InstanceType.upsert(awsProvider.code, "test-i1", 3, 5.0, details);
    JsonNode json = doGetInstanceTypeAndVerify(awsProvider.uuid, it.getInstanceTypeCode(), OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "memSizeGB", "5.0");
    assertValue(json, "numCores", "3");
    assertValue(json, "active", "true");
    JsonNode volumeDetailsListNode = json.get("instanceTypeDetails").get("volumeDetailsList");
    assertNotNull(volumeDetailsListNode);
    for (int i = 0; i < 2; ++i) {
      JsonNode machineDetailsNode = volumeDetailsListNode.get(i);
      assertThat(machineDetailsNode, notNullValue());
      assertValue(machineDetailsNode, "volumeSizeGB", "20");
      assertValue(machineDetailsNode, "volumeType", "SSD");
      assertValue(machineDetailsNode, "mountPath", String.format("/mnt/d%d", i));
    }
  }

  @Test
  public void testGetInstanceTypeWithInvalidParams() {
    String fakeInstanceCode = "foo";
    JsonNode json = doGetInstanceTypeAndVerify(awsProvider.uuid, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Instance Type not found: " + fakeInstanceCode);
  }

  @Test
  public void testGetInstanceTypeWithInvalidProvider() {
    String fakeInstanceCode = "foo";
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doGetInstanceTypeAndVerify(randomUUID, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }

  @Test
  public void testDeleteInstanceTypeWithValidParams() {
    InstanceType it = InstanceType.upsert(awsProvider.code, "test-i1", 3, 5.0,
        new InstanceType.InstanceTypeDetails());
    JsonNode json = doDeleteInstanceTypeAndVerify(awsProvider.uuid, it.getInstanceTypeCode(), OK);
    it = InstanceType.get(awsProvider.code, it.getInstanceTypeCode());
    assertTrue(json.get("success").asBoolean());
    assertFalse(it.isActive());
  }

  @Test
  public void testDeleteInstanceTypeWithInvalidParams() {
    String fakeInstanceCode = "foo";
    JsonNode json = doDeleteInstanceTypeAndVerify(awsProvider.uuid, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Instance Type not found: " + fakeInstanceCode);
  }

  @Test
  public void testDeleteInstanceTypeWithInvalidProvider() {
    String fakeInstanceCode = "foo";
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doDeleteInstanceTypeAndVerify(randomUUID, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }
}
