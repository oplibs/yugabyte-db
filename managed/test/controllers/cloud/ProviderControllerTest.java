// Copyright (c) Yugabyte, Inc.

package controllers.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import helpers.FakeDBApplication;
import models.cloud.Provider;
import models.yb.Customer;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.route;

public class ProviderControllerTest extends FakeDBApplication {
	Customer customer;

	@Before
	public void setUp() {
		customer = Customer.create("Valid Customer", "foo@bar.com", "password");
	}

	@Test
	public void testListEmptyProviders() {
		String authToken = customer.createAuthToken();
		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(controllers.cloud.routes.ProviderController.list())
				.header("X-AUTH-TOKEN", authToken);
		Result result = route(fr);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(OK, result.status());
		assertEquals("[]", json.toString());
		assertEquals(0, json.size());
	}

	@Test
	public void testListProviders() {
		String authToken = customer.createAuthToken();
		Provider p1 = Provider.create("Amazon");
		Provider p2 = Provider.create("Google");

		Http.RequestBuilder request = play.test.Helpers.fakeRequest(controllers.cloud.routes.ProviderController.list())
				.header("X-AUTH-TOKEN", authToken);
		Result result = route(request);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(OK, result.status());
		assertEquals(2, json.size());
		assertEquals(json.get(0).path("uuid").asText(), p1.uuid.toString());
		assertEquals(json.get(0).path("name").asText(), p1.name.toString());
		assertEquals(json.get(1).path("uuid").asText(), p2.uuid.toString());
		assertEquals(json.get(1).path("name").asText(), p2.name.toString());
	}

	@Test
	public void testCreateProvider() {
		String authToken = customer.createAuthToken();
		ObjectNode bodyJson = Json.newObject();
		bodyJson.put("name", "Microsoft");

		Http.RequestBuilder request = play.test.Helpers.fakeRequest(controllers.cloud.routes.ProviderController.create())
				.header("X-AUTH-TOKEN", authToken)
				.bodyJson(bodyJson);
		Result result = route(request);
		JsonNode json = Json.parse(contentAsString(result));
		assertEquals(OK, result.status());
		assertEquals(1, json.findValues("name").size());
		assertEquals(json.path("name").asText(), "Microsoft");
	}

	@Test
	public void testCreateDuplicateProvider() {
		String authToken = customer.createAuthToken();

		Provider.create("Amazon");

		ObjectNode bodyJson = Json.newObject();
		bodyJson.put("name", "Amazon");

		Http.RequestBuilder request = play.test.Helpers.fakeRequest(controllers.cloud.routes.ProviderController.create())
				.header("X-AUTH-TOKEN", authToken)
				.bodyJson(bodyJson);
		Result result = route(request);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(INTERNAL_SERVER_ERROR, result.status());
		assertThat(json.get("error").toString(), allOf(notNullValue(), containsString("Unique index or primary key violation:")));
	}
}
