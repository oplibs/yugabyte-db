// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Matchers.anyString;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.MetricConfig;
import java.util.HashMap;
import java.util.Map;
import org.hamcrest.core.AllOf;
import org.hamcrest.core.IsEqual;
import org.hamcrest.core.IsInstanceOf;
import org.hamcrest.core.IsNull;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryExecutorTest extends FakeDBApplication {
  @Mock play.Configuration mockAppConfig;

  @Mock ApiHelper mockApiHelper;

  @Mock YBMetricQueryComponent mockYBMetricQueryComponent;

  private MetricConfig validMetric;
  private MetricConfig validRangeMetric;

  @Before
  public void setUp() {
    when(mockAppConfig.getString("yb.metrics.url")).thenReturn("foo://bar");

    JsonNode configJson =
        Json.parse(
            "{\"metric\": \"our_valid_metric\", "
                + "\"function\": \"sum\", \"filters\": {\"filter\": \"awesome\"},"
                + "\"layout\": {\"title\": \"Awesome Metric\", "
                + "\"xaxis\": { \"type\": \"date\" }}}");
    validMetric = MetricConfig.create("valid_metric", configJson);
    validMetric.save();

    JsonNode rangeConfigJson =
        Json.parse(
            "{\"metric\": \"our_valid_range_metric\", "
                + "\"function\": \"avg_over_time|avg\", \"range\": true,"
                + "\"filters\": {\"filter\": \"awesome\"},"
                + "\"layout\": {\"title\": \"Awesome Metric\", "
                + "\"xaxis\": { \"type\": \"date\" }}}");
    validRangeMetric = MetricConfig.create("valid_range_metric", rangeConfigJson);
    validRangeMetric.save();
  }

  @Test
  public void testWithValidMetric() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig, mockApiHelper, params, new HashMap<>(), mockYBMetricQueryComponent);

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    when(mockApiHelper.getRequest(eq("foo://bar/query"), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));

    JsonNode result = qe.call();
    assertThat(
        result.get("queryKey").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("valid_metric")));

    JsonNode data = result.get("data");
    assertThat(data, AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertEquals(data.size(), 2);
    for (int i = 0; i < data.size(); i++) {
      assertThat(
          data.get(i).get("name").asText(),
          AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("system")));
      assertThat(
          data.get(i).get("type").asText(),
          AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("scatter")));
      assertThat(
          data.get(i).get("x"),
          AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
      assertThat(
          data.get(i).get("y"),
          AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    }

    JsonNode layout = result.get("layout");
    assertThat(layout, AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertThat(
        layout.get("title").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("Awesome Metric")));
    assertThat(
        layout.get("xaxis"),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertThat(
        layout.get("xaxis").get("type").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("date")));
  }

  @Test
  public void testWithValidRangeMetric() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_range_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig,
            mockApiHelper,
            params,
            new HashMap<>(),
            mockYBMetricQueryComponent,
            new MetricSettings()
                .setMetric("valid_range_metric")
                .setAggregation(MetricAggregation.MAX),
            false);

    JsonNode result = qe.call();
    String directUrl = result.get("directURL").asText();
    assertEquals(
        directUrl,
        "foo://bar/graph?g0.expr=max%28max_over_time%28"
            + "our_valid_range_metric%7Bfilter%3D%22awesome%22%7D%5B0s%5D%29%29&g0.tab=0"
            + "&g0.range_input=3600s&g0.end_input=");
  }

  @Test
  public void testWithInvalidMetric() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "invalid_metric");

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig, mockApiHelper, params, new HashMap<>(), mockYBMetricQueryComponent);
    JsonNode result = qe.call();

    assertThat(
        result.get("queryKey").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("invalid_metric")));
    assertThat(
        result.get("error").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("Invalid Query Key")));
  }

  @Test
  public void testQueryWithEndDate() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("end", "1479281937");
    params.put("queryKey", "valid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig, mockApiHelper, params, new HashMap<>(), mockYBMetricQueryComponent);

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));
    qe.call();
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(
        queryUrl.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("foo://bar/query_range")));

    assertThat(
        queryParam.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));
    assertThat(
        queryParam.getValue().toString(),
        AllOf.allOf(
            IsNull.notNullValue(),
            IsEqual.equalTo(
                "{start=1479281737, queryKey=valid_metric, "
                    + "end=1479281937, query=sum(our_valid_metric{filter=\"awesome\"})}")));
  }

  @Test
  public void testQueryWithoutEndDate() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig, mockApiHelper, params, new HashMap<>(), mockYBMetricQueryComponent);

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));
    qe.call();
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(
        queryUrl.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("foo://bar/query")));

    assertThat(
        queryParam.getValue(),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));
    assertThat(
        queryParam.getValue().toString(),
        AllOf.allOf(
            IsNull.notNullValue(),
            IsEqual.equalTo(
                "{start=1479281737, queryKey="
                    + "valid_metric, query=sum(our_valid_metric{filter=\"awesome\"})}")));
  }

  @Test
  public void testInvalidQuery() throws Exception {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");

    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig, mockApiHelper, params, new HashMap<>(), mockYBMetricQueryComponent);

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"error\",\"errorType\":\"bad_data\","
                + "\"error\":\"parse error at char 44: unexpected \\\"{\\\" in aggregation, expected \\\")\\\"\"}");
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.toJson(responseJson));
    JsonNode response = qe.call();
    assertThat(
        response.get("error").asText(),
        AllOf.allOf(
            IsNull.notNullValue(),
            IsEqual.equalTo(
                "parse error at char 44: unexpected " + "\"{\" in aggregation, expected \")\"")));
  }

  @Test
  public void testNativeMetrics() throws Exception {
    when(mockAppConfig.getBoolean(eq("yb.metrics.useNative"), eq(false))).thenReturn(true);
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("queryKey", "valid_metric");
    MetricQueryExecutor qe =
        new MetricQueryExecutor(
            mockAppConfig, mockApiHelper, params, new HashMap<>(), mockYBMetricQueryComponent);

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"cpu\":\"system\"}, \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    when(mockYBMetricQueryComponent.query(anyMap())).thenReturn(Json.toJson(responseJson));

    JsonNode result = qe.call();
    assertThat(
        result.get("queryKey").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("valid_metric")));

    JsonNode data = result.get("data");
    assertThat(data, AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertEquals(2, data.size());
    for (int i = 0; i < data.size(); i++) {
      assertThat(
          data.get(i).get("name").asText(),
          AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("system")));
      assertThat(
          data.get(i).get("type").asText(),
          AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("scatter")));
      assertThat(
          data.get(i).get("x"),
          AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
      assertThat(
          data.get(i).get("y"),
          AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    }

    JsonNode layout = result.get("layout");
    assertThat(layout, AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertThat(
        layout.get("title").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("Awesome Metric")));
    assertThat(
        layout.get("xaxis"),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertThat(
        layout.get("xaxis").get("type").asText(),
        AllOf.allOf(IsNull.notNullValue(), IsEqual.equalTo("date")));
  }
}
