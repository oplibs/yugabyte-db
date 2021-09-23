// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertYWSE;
import static com.yugabyte.yw.common.ReleaseManager.ReleaseState.DISABLED;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyObject;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;

public class ReleaseControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  private Result getReleases(UUID customerUUID) {
    return getReleases(customerUUID, false);
  }

  private Result getReleases(UUID customerUUID, boolean includeMetadata) {
    String uri = "/api/customers/" + customerUUID + "/releases";
    if (includeMetadata) {
      uri = uri + "?includeMetadata=true";
    }
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result refreshReleases(UUID customerUUID) {
    String uri = "/api/customers/" + customerUUID + "/releases";
    return FakeApiHelper.doRequestWithAuthToken("PUT", uri, user.createAuthToken());
  }

  private Result createRelease(UUID customerUUID, JsonNode body) {
    String uri = "/api/customers/" + customerUUID + "/releases";
    return FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, user.createAuthToken(), body);
  }

  private Result updateRelease(UUID customerUUID, String version, JsonNode body) {
    String uri = "/api/customers/" + customerUUID + "/releases/" + version;
    return FakeApiHelper.doRequestWithAuthTokenAndBody("PUT", uri, user.createAuthToken(), body);
  }

  private Result deleteRelease(UUID customerUUID, String version) {
    String uri = "/api/customers/" + customerUUID + "/releases/" + version;
    return FakeApiHelper.doRequestWithAuthToken("DELETE", uri, user.createAuthToken());
  }

  private void mockReleaseData(boolean multiple) {
    ImmutableMap<String, Object> data;
    if (multiple) {
      ReleaseManager.ReleaseMetadata activeRelease =
          ReleaseManager.ReleaseMetadata.fromLegacy("0.0.3", "yugabyte-0.0.3.tar.gz");
      ReleaseManager.ReleaseMetadata disabledRelease =
          ReleaseManager.ReleaseMetadata.fromLegacy("0.0.2", "yugabyte-0.0.2.tar.gz");
      disabledRelease.state = DISABLED;
      ReleaseManager.ReleaseMetadata deletedRelease =
          ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "yugabyte-0.0.1.tar.gz");
      deletedRelease.state = ReleaseManager.ReleaseState.DELETED;
      data =
          ImmutableMap.of(
              "0.0.3", activeRelease,
              "0.0.2", disabledRelease,
              "0.0.1", deletedRelease);
    } else {
      ReleaseManager.ReleaseMetadata activeRelease =
          ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "yugabyte-0.0.1.tar.gz");
      data = ImmutableMap.of("0.0.1", activeRelease);
    }

    when(mockReleaseManager.getReleaseMetadata()).thenReturn(data);
  }

  private void assertReleases(Map expectedMap, HashMap releases) {
    for (Object version : releases.keySet()) {
      assertTrue(expectedMap.containsKey(version));
      Object metadata = expectedMap.get(version);
      JsonNode releaseJson = Json.toJson(releases.get(version));
      String expectedFilePath;
      String expectedState = "ACTIVE";
      if (metadata instanceof Map) {
        expectedFilePath = ((Map) metadata).get("filePath").toString();
        expectedState = ((Map) metadata).get("state").toString();
      } else {
        expectedFilePath = metadata.toString();
      }
      assertValue(releaseJson, "filePath", expectedFilePath);
      assertValue(releaseJson, "imageTag", version.toString());
      assertValue(releaseJson, "state", expectedState);
    }
  }

  @Test
  public void testCreateS3Release() {
    ObjectNode pathsNode =
        (ObjectNode)
            Json.newObject()
                .put(
                    "x86_64",
                    "s3://releases.yugabyte.com/2.7.2.0-b137/"
                        + "yugabyte-2.7.2.0-b137-centos-x86_64.tar.gz");
    ObjectNode s3 =
        (ObjectNode)
            Json.newObject()
                .put("accessKeyId", "AAAAAAAAAAAAAAAAAAAA")
                .put("secretAccessKey", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA+Vlvn/W")
                .set("paths", pathsNode);

    ObjectNode body = (ObjectNode) Json.newObject().set("foo", Json.newObject().set("s3", s3));
    Result result = createRelease(customer.uuid, body);
    verify(mockReleaseManager, times(1)).addReleaseWithMetadata(anyString(), anyObject());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateGCSRelease() {
    ObjectNode pathsNode =
        (ObjectNode)
            Json.newObject()
                .put(
                    "x86_64",
                    "gs://my-gcs-buucket/2.7.2.0-b137/"
                        + "yugabyte-2.7.2.0-b137-centos-x86_64.tar.gz");
    ObjectNode gcs =
        (ObjectNode) Json.newObject().put("credentialsJson", "{}").set("paths", pathsNode);

    ObjectNode body = (ObjectNode) Json.newObject().set("foo", Json.newObject().set("gcs", gcs));
    Result result = createRelease(customer.uuid, body);
    verify(mockReleaseManager, times(1)).addReleaseWithMetadata(anyString(), anyObject());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateReleaseWithInvalidCustomer() {
    ObjectNode body = Json.newObject();
    UUID randomUUID = UUID.randomUUID();
    Result result = createRelease(randomUUID, body);
    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateReleaseWithReleaseManagerException() {

    ObjectNode pathsNode =
        (ObjectNode)
            Json.newObject()
                .put(
                    "x86_64",
                    "s3://releases.yugabyte.com/2.7.2.0-b137/"
                        + "yugabyte-2.7.2.0-b137-centos-x86_64.tar.gz");
    ObjectNode s3 =
        (ObjectNode)
            Json.newObject()
                .put("accessKeyId", "AAAAAAAAAAAAAAAAAAAA")
                .put("secretAccessKey", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA+Vlvn/W")
                .set("paths", pathsNode);

    ObjectNode body =
        (ObjectNode) Json.newObject().set("2.7.2.0-b137", Json.newObject().set("s3", s3));
    doThrow(new PlatformServiceException(BAD_REQUEST, "Some Error"))
        .when(mockReleaseManager)
        .addReleaseWithMetadata(any(), any());
    Result result = assertYWSE(() -> createRelease(customer.uuid, body));
    verify(mockReleaseManager, times(1)).addReleaseWithMetadata(any(), any());
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateReleaseInvalidParams() {

    ObjectNode pathsNode = (ObjectNode) Json.newObject().put("x86_64", "s3://foobar");
    ObjectNode s3 =
        (ObjectNode)
            Json.newObject()
                .put("accessKeyId", "AAAAAAAAAAAAAAAAAAAA")
                .put("secretAccessKey", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA+Vlvn/W")
                .set("paths", pathsNode);
    ObjectNode http = (ObjectNode) Json.newObject().set("paths", pathsNode);

    pathsNode.put("x86_64", "a3://foobar");

    ObjectNode body =
        (ObjectNode) Json.newObject().set("2.7.2.0-b137", Json.newObject().set("s3", s3));
    Result result = assertYWSE(() -> createRelease(customer.uuid, body));
    verify(mockReleaseManager, times(0)).addReleaseWithMetadata(any(), any());
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);

    pathsNode.put("x86_64", "s3://foobar");
    s3.remove("accessKeyId");

    ObjectNode body2 =
        (ObjectNode) Json.newObject().set("2.7.2.0-b137", Json.newObject().set("s3", s3));
    result = assertYWSE(() -> createRelease(customer.uuid, body2));
    verify(mockReleaseManager, times(0)).addReleaseWithMetadata(any(), any());
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);

    ObjectNode body3 =
        (ObjectNode) Json.newObject().set("2.7.2.0-b137", Json.newObject().set("http", http));
    result = assertYWSE(() -> createRelease(customer.uuid, body3));
    verify(mockReleaseManager, times(0)).addReleaseWithMetadata(any(), any());
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    assertAuditEntry(0, customer.uuid);

    pathsNode.put("x86_64_checksum", "foo");
    pathsNode.put("x86_64", "https://foobar.com");
    ObjectNode body4 =
        (ObjectNode) Json.newObject().set("2.7.2.0-b137", Json.newObject().set("http", http));
    result = createRelease(customer.uuid, body4);
    verify(mockReleaseManager, times(1)).addReleaseWithMetadata(anyString(), anyObject());
    assertOk(result);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testGetReleasesLegacy() {
    mockReleaseData(false);
    Result result = getReleases(customer.uuid);
    verify(mockReleaseManager, times(1)).getReleaseMetadata();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals("0.0.1", json.get(0).asText());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetReleaseWithGetReleaseMetadataException() {
    doThrow(new PlatformServiceException(BAD_REQUEST, "Some Error"))
        .when(mockReleaseManager)
        .getReleaseMetadata();
    Result result = assertYWSE(() -> getReleases(customer.uuid));
    assertBadRequest(result, "Some Error");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetReleaseWithReleaseManagerEmpty() {
    HashMap response = new HashMap();
    when(mockReleaseManager.getReleaseMetadata()).thenReturn(response);
    Result result = getReleases(customer.uuid);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetReleasesWithStateFiltering() {
    mockReleaseData(true);
    Result result = getReleases(customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(2, json.size());
    assertEquals("0.0.2", json.get(0).asText());
    assertEquals("0.0.3", json.get(1).asText());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetReleasesWithMetadata() {
    mockReleaseData(false);
    Result result = getReleases(customer.uuid, true);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    HashMap releases = Json.fromJson(json, HashMap.class);

    assertTrue(json.has("0.0.1"));
    Map expectedMap =
        ImmutableMap.of(
            "0.0.1", ImmutableMap.of("filePath", "yugabyte-0.0.1.tar.gz", "state", "ACTIVE"));
    assertReleases(expectedMap, releases);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetReleasesStateFilteringWithMetadata() {
    mockReleaseData(true);
    Map expectedMap =
        ImmutableMap.of(
            "0.0.3",
            ImmutableMap.of("filePath", "yugabyte-0.0.3.tar.gz", "state", "ACTIVE"),
            "0.0.2",
            ImmutableMap.of("filePath", "yugabyte-0.0.2.tar.gz", "state", "DISABLED"));
    Result result = getReleases(customer.uuid, true);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    HashMap releases = Json.fromJson(json, HashMap.class);
    assertReleases(expectedMap, releases);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUpdateRelease() {
    ReleaseManager.ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.1");
    when(mockReleaseManager.getReleaseByVersion("0.0.1")).thenReturn(metadata);
    ObjectNode body = Json.newObject();
    body.put("version", "0.0.1");
    body.put("state", "DISABLED");
    Result result = updateRelease(customer.uuid, "0.0.1", body);
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.1");
    ArgumentCaptor<String> expectedVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<ReleaseManager.ReleaseMetadata> expectedReleaseMetadata =
        ArgumentCaptor.forClass(ReleaseManager.ReleaseMetadata.class);
    verify(mockReleaseManager, times(1))
        .updateReleaseMetadata(expectedVersion.capture(), expectedReleaseMetadata.capture());

    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals(expectedVersion.getValue(), "0.0.1");
    assertEquals(expectedReleaseMetadata.getValue().state, DISABLED);
    assertValue(json, "state", "DISABLED");
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUpdateReleaseWithInvalidVersion() {
    ObjectNode body = Json.newObject();
    body.put("state", "DISABLED");
    Result result = assertYWSE(() -> updateRelease(customer.uuid, "0.0.2", body));
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.2");
    assertBadRequest(result, "Invalid Release version: 0.0.2");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUpdateReleaseWithReleaseManagerException() {
    doThrow(new PlatformServiceException(BAD_REQUEST, "Some Error"))
        .when(mockReleaseManager)
        .getReleaseByVersion("0.0.2");
    ObjectNode body = Json.newObject();
    body.put("state", "DISABLED");
    Result result = assertYWSE(() -> updateRelease(customer.uuid, "0.0.2", body));
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.2");
    assertBadRequest(result, "Some Error");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUpdateReleaseWithMissingStateParam() {
    ReleaseManager.ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.1");
    when(mockReleaseManager.getReleaseByVersion("0.0.1")).thenReturn(metadata);
    ObjectNode body = Json.newObject();
    Result result = assertYWSE(() -> updateRelease(customer.uuid, "0.0.1", body));
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.1");
    assertBadRequest(result, "Missing Required param: State");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testRefreshRelease() {
    Result result = refreshReleases(customer.uuid);
    verify(mockReleaseManager, times(1)).importLocalReleases();
    assertOk(result);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testRefreshReleaseInvalidCustomer() {
    UUID randomUUID = UUID.randomUUID();
    Result result = refreshReleases(randomUUID);
    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testRefreshReleaseReleaseManagerException() {
    doThrow(new RuntimeException("Some Error")).when(mockReleaseManager).importLocalReleases();
    Result result = assertYWSE(() -> refreshReleases(customer.uuid));
    assertInternalServerError(result, "Some Error");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testDeleteInvalidRelease() {
    Result result = assertYWSE(() -> deleteRelease(customer.uuid, "0.0.1"));
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.1");
    assertBadRequest(result, "Invalid Release version: 0.0.1");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testDeleteReleaseSuccess() {
    ReleaseManager.ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.2");
    when(mockReleaseManager.getReleaseByVersion("0.0.2")).thenReturn(metadata);
    Result result = deleteRelease(customer.uuid, "0.0.2");
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.2");
    assertOk(result);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testDeleteReleaseWithException() {
    ReleaseManager.ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.3");
    when(mockReleaseManager.getReleaseByVersion("0.0.3")).thenReturn(metadata);
    doThrow(new RuntimeException("Some Error")).when(mockReleaseManager).removeRelease("0.0.3");
    Result result = assertYWSE(() -> deleteRelease(customer.uuid, "0.0.3"));
    verify(mockReleaseManager, times(1)).getReleaseByVersion("0.0.3");
    assertInternalServerError(result, "Some Error");
    assertAuditEntry(0, customer.uuid);
  }
}
