// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorResponse;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.YugawareProperty;

import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.models.helpers.NodeDetails;
import org.hamcrest.CoreMatchers;
import org.hamcrest.core.AllOf;
import org.hamcrest.core.Is;
import org.hamcrest.core.IsInstanceOf;
import org.hamcrest.core.IsNull;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class RegionControllerTest extends FakeDBApplication {
  Provider provider;
  Customer customer;
  Users user;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    provider = ModelFactory.awsProvider(customer);
  }

  private Result listRegions(UUID providerUUID) {
    String uri =
        String.format("/api/customers/%s/providers/%s/regions", customer.uuid, providerUUID);
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result listAllRegions() {
    String uri = String.format("/api/customers/%s/regions", customer.uuid);
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result createRegion(UUID providerUUID, JsonNode body) {
    String uri =
        String.format("/api/customers/%s/providers/%s/regions", customer.uuid, providerUUID);
    return FakeApiHelper.doRequestWithBody("POST", uri, body);
  }

  private Result deleteRegion(UUID providerUUID, UUID regionUUID) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/regions/%s", customer.uuid, providerUUID, regionUUID);
    return FakeApiHelper.doRequest("DELETE", uri);
  }

  private Result editRegion(UUID providerUUID, UUID regionUUID, JsonNode body) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/regions/%s", customer.uuid, providerUUID, regionUUID);
    return FakeApiHelper.doRequestWithBody("PUT", uri, body);
  }

  @Test
  public void testListRegionsWithInvalidProviderUUID() {
    Result result = listRegions(UUID.randomUUID());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListEmptyRegionsWithValidProviderUUID() {
    Result result = listRegions(provider.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListAllRegionsWithValidRegion() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = listAllRegions();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    JsonNode regionJson = json.get(0);
    JsonNode providerJson = regionJson.get("provider");
    JsonNode zonesJson = regionJson.get("zones");
    assertNotNull(providerJson);
    assertEquals(1, zonesJson.size());
    assertValue(regionJson, "uuid", r.uuid.toString());
    assertValue(providerJson, "uuid", provider.uuid.toString());
    assertValue(zonesJson.get(0), "uuid", az.uuid.toString());
    assertValue(zonesJson.get(0), "code", az.code);
    assertValue(zonesJson.get(0), "subnet", az.subnet);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListAllRegionsWithNoRegion() {
    Result result = listAllRegions();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListRegionWithoutZonesAndValidProviderUUID() {
    Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    Result result = listRegions(provider.uuid);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListRegionsWithValidProviderUUID() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone.createOrThrow(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = listRegions(provider.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r.code);
    assertEquals(json.get(0).path("name").asText(), r.name);
    assertThat(
        json.get(0).path("zones"),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListRegions() {
    Region r1 = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    Region r2 = Region.create(provider, "region-2", "PlacementRegion 2", "default-image");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ-1.2", "PlacementAZ 1.2", "Subnet - 1.2");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ-1.3", "PlacementAZ 1.3", "Subnet - 1.3");
    AvailabilityZone.createOrThrow(r2, "PlacementAZ-2.1", "PlacementAZ 2.1", "Subnet - 2.1");
    AvailabilityZone.createOrThrow(r2, "PlacementAZ-2.2", "PlacementAZ 2.2", "Subnet - 2.2");
    Result result = listRegions(provider.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(2, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateRegionsWithInvalidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    UUID randomUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> createRegion(randomUUID, regionJson));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorResponse(result, "Invalid Provider UUID: " + randomUUID);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateRegionsWithoutRequiredParams() {
    Result result = assertPlatformException(() -> createRegion(provider.uuid, Json.newObject()));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result),
        CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateRegionsWithValidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("name", "Foo PlacementRegion");
    Result result = createRegion(provider.uuid, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), Is.is(IsNull.notNullValue()));
    assertValue(json, "code", "foo-region");
    assertValue(json, "name", "Foo PlacementRegion");
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateRegionsWithValidGCPRegion() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "us-west1");
    regionJson.put("name", "Gcp US West 1");
    Provider gcpProvider = ModelFactory.gcpProvider(customer);
    Result result = createRegion(gcpProvider.uuid, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), Is.is(IsNull.notNullValue()));
    assertValue(json, "code", "us-west1");
    assertValue(json, "name", "Gcp US West 1");
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateRegionWithMetadataValidVPCInfo() {
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"foo-region\": {\"name\": \"Foo Region\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("hostVpcRegion", "host-vpc-region");
    regionJson.put("hostVpcId", "host-vpc-id");
    regionJson.put("destVpcId", "dest-vpc-id");
    JsonNode vpcInfo = Json.parse("{\"foo-region\": {\"zones\": {\"zone-1\": \"subnet-1\"}}}");
    // TODO:
    when(mockNetworkManager.bootstrap(any(), any(), any())).thenReturn(vpcInfo);
    Result result = createRegion(provider.uuid, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode details = json.get("details");
    JsonNode cloudInfo = details.get("cloudInfo");
    JsonNode awsRegionCloudInfo = cloudInfo.get("aws");
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), Is.is(IsNull.notNullValue()));
    assertValue(json, "code", "foo-region");
    assertValue(json, "name", "Foo Region");
    assertValue(awsRegionCloudInfo, "ybImage", "yb image");
    assertNotNull(json.get("zones"));
    Region r = Region.getByCode(provider, "foo-region");
    assertEquals(1, r.zones.size());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateRegionWithMetadataInvalidVPCInfo() {
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"foo-region\": {\"name\": \"Foo Region\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("hostVpcRegion", "host-vpc-region");
    regionJson.put("hostVpcId", "host-vpc-id");
    regionJson.put("destVpcId", "dest-vpc-id");
    ObjectNode vpcInfo = Json.newObject();
    vpcInfo.put("error", "Something went wrong!!.");
    when(mockNetworkManager.bootstrap(any(), any(), any())).thenReturn(vpcInfo);
    Result result = assertPlatformException(() -> createRegion(provider.uuid, regionJson));
    assertInternalServerError(result, "Region Bootstrap failed.");
    Region r = Region.getByCode(provider, "foo-region");
    assertNull(r);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateRegionsWithLongRegionName() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "datacenter-azure-washington");
    regionJson.put("name", "Gcp US West 1");
    Result result = assertPlatformException(() -> createRegion(provider.uuid, regionJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "success", "false");
    assertNotNull(json.get("error"));
    assertEquals(json.get("error").get("code").get(0).asText(), "Maximum length is 25");
  }

  @Test
  public void testDeleteRegionWithInvalidParams() {
    UUID randomUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> deleteRegion(provider.uuid, randomUUID));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result), CoreMatchers.containsString("Invalid Provider/Region UUID"));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testDeleteRegionWithValidParams() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");

    AvailabilityZone.createOrThrow(r, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "AZ 2", "subnet-2");

    Region actualRegion = getFirstRegion();
    assertTrue(actualRegion.isActive());
    for (AvailabilityZone az : actualRegion.zones) {
      assertTrue(az.active);
    }

    Result result = deleteRegion(provider.uuid, r.uuid);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("success").asBoolean());

    assertNull(Region.get(r.uuid));
  }

  @Test
  public void testDeleteRegionInUseByUniverses() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");

    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "AZ 2", "subnet-2");

    Universe universe = ModelFactory.createUniverse();
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    Set<NodeDetails> nodeDetailsSet = ApiUtils.getDummyNodeDetailSet(UUID.randomUUID(), 3, 3);
    udtp.nodeDetailsSet = nodeDetailsSet;

    int i = 0;
    for (NodeDetails nd : nodeDetailsSet) {
      nd.azUuid = i++ % 2 == 0 ? az1.uuid : az2.uuid;
    }

    universe.setUniverseDetails(udtp);
    universe.update();

    Result result = assertPlatformException(() -> deleteRegion(provider.uuid, r.uuid));
    assertEquals(FORBIDDEN, result.status());

    assertNotNull(Region.get(r.uuid));
  }

  @Test
  public void testEditRegion() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    ObjectNode regionJson = Json.newObject();
    String updatedYbImage = "another-image";
    String updatedSG = "sg-123456";
    regionJson.put("ybImage", updatedYbImage);
    regionJson.put("securityGroupId", updatedSG);

    Result result = editRegion(provider.uuid, r.uuid, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode details = json.get("details");
    JsonNode cloudInfo = details.get("cloudInfo");
    JsonNode awsRegionCloudInfo = cloudInfo.get("aws");

    assertEquals(OK, result.status());
    assertValue(awsRegionCloudInfo, "ybImage", updatedYbImage);
    assertValue(awsRegionCloudInfo, "securityGroupId", updatedSG);

    r.refresh();
    assertEquals(updatedSG, r.getSecurityGroupId());
    assertEquals(updatedYbImage, r.getYbImage());

    regionJson.put("securityGroupId", (String) null);
    result = editRegion(provider.uuid, r.uuid, regionJson);
    json = Json.parse(contentAsString(result));
    details = json.get("details");
    cloudInfo = details.get("cloudInfo");
    awsRegionCloudInfo = cloudInfo.get("aws");
    assertEquals(OK, result.status());
    assertValue(awsRegionCloudInfo, "ybImage", updatedYbImage);
    assertNull(awsRegionCloudInfo.get("securityGroupId"));

    r.refresh();
    assertNull(r.getSecurityGroupId());
    assertEquals(updatedYbImage, r.getYbImage());
  }

  public Region getFirstRegion() {
    Result result;
    result = listAllRegions();
    assertEquals(OK, result.status());
    return Json.fromJson(Json.parse(contentAsString(result)).get(0), Region.class);
  }
}
