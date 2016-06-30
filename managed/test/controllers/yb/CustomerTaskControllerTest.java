// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import helpers.ApiHelper;
import helpers.FakeDBApplication;
import models.yb.Customer;
import models.yb.CustomerTask;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

import java.util.Map;
import java.util.Optional;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.*;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

public class CustomerTaskControllerTest extends FakeDBApplication {
  private Customer customer;
  private ApiHelper mockApiHelper;

  @Override
  protected Application provideApplication() {
    mockApiHelper = mock(ApiHelper.class);
    return new GuiceApplicationBuilder()
      .configure((Map) Helpers.inMemoryDatabase())
      .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
      .build();
  }

  @Before
  public void setUp() { customer = Customer.create("Valid Customer", "foo@bar.com", "password"); }

  @Test
  public void testTaskHistoryEmptyListAsJSON() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.JSON)
    );

    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(0, json.size());
  }

  @Test
  public void testTaskHistoryEmptyListAsHTML() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.HTML)
    );
    assertEquals(OK, result.status());
    assertThat(result.contentType(), is(equalTo(Optional.of(Http.MimeTypes.HTML))));
    assertThat(contentAsString(result).trim(), is(equalTo("")));
  }

  @Test
  public void testTaskHistoryEmptyListUnknownContentType() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.TEXT)
    );

    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testTaskHistoryListAsJSON() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "50");
    when(mockApiHelper.getRequest(Matchers.any(String.class))).thenReturn(getResponseJson);

    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.JSON)
    );

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertThat(json.get(0).get("id").asText(), allOf(notNullValue(), equalTo(taskUUID.toString())));
    assertThat(json.get(0).get("title").asText(), allOf(notNullValue(), equalTo("Creating Instance : Foo")));
    assertThat(json.get(0).get("percentComplete").asInt(), allOf(notNullValue(), equalTo(50)));
    assertThat(json.get(0).get("success").asBoolean(), allOf(notNullValue(), equalTo(true)));
  }

  @Test
  public void testTaskHistoryListAsHTML() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "50");
    when(mockApiHelper.getRequest(Matchers.any(String.class))).thenReturn(getResponseJson);

    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.HTML)
    );

    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("Creating Instance : Foo")));
  }

  @Test
  public void testTaskHistoryProgressCompletes() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "100");
    when(mockApiHelper.getRequest(Matchers.any(String.class))).thenReturn(getResponseJson);

    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.HTML)
    );

    CustomerTask ct = CustomerTask.find.where().eq("task_uuid", taskUUID.toString()).findUnique();

    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("Created Instance : Foo")));
    assertTrue(ct.getCreateTime().before(ct.getCompletionTime()));
  }
}
