package controllers.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import java.util.Map;

import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.node.ObjectNode;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

public class TasksControllerTest extends WithApplication {

  @Override
  protected Application provideApplication() {
      return new GuiceApplicationBuilder()
          .configure((Map) Helpers.inMemoryDatabase())
          .build();
  }

  @Before
  public void setUp() {
  }

  @Test
  public void testCreateTask() {
    ObjectNode createInstanceJson = Json.newObject();
    createInstanceJson.put("instanceUUID", "11111111-2222-3333-4444-555555555555");
    createInstanceJson.put("instanceName", "TestInstance");
    Result result = route(fakeRequest("POST", "/commissioner/tasks").bodyJson(createInstanceJson));
    assertEquals(OK, result.status());
    assertNotNull(contentAsString(result));
  }
}
