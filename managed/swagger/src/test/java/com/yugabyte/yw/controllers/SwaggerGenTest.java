/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static junit.framework.TestCase.fail;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.route;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Lists;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.swagger.PlatformModelConverter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;
import org.apache.commons.io.IOUtils;
import org.junit.Test;
import play.api.routing.Router;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.modules.swagger.SwaggerModule;
import play.mvc.Result;
import play.test.Helpers;

/**
 * Unittest can be run like this: [yugaware] $ testOnly com.yugabyte.yw.controllers.SwaggerGenTest
 * This test will fail if swagger test has not been generated.
 *
 * <p>Run following command to update the spec so that the test passes: [yugaware] swaggerGen
 */
public class SwaggerGenTest extends FakeDBApplication {

  @Override
  protected boolean isSwaggerEnabled() {
    return true;
  }

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    return super.configureApplication(
        builder
            .overrides(new SwaggerModule())
            .overrides(bind(Router.class).toProvider(SwaggerGenRoutesProvider.class)));
  }

  @Test
  public void genJson() throws IOException {
    String resourceName = "swagger.json";
    System.err.println(app.classloader().getResource(resourceName).getFile());
    try (InputStream is = app.classloader().getResourceAsStream(resourceName)) {
      String expectedSwagger = IOUtils.toString(is, StandardCharsets.UTF_8);
      String actualSwaggerSpec = getSwaggerSpec();
      if (actualSwaggerSpec.length() != expectedSwagger.length()) {
        // TODO: Fix this: Only length comparison because the json ordering change
        // There still may be some ordering changes depending on machine architecture since
        // the json still has few lists that are not sorted. But most likely this is a non-issue.
        File outFile = File.createTempFile("swagger", ".json");
        try (FileWriter fileWriter = new FileWriter(outFile)) {
          fileWriter.write(actualSwaggerSpec);
        }
        fail(
            "==============================================================================\n"
                + "Run $sbt swaggerGen\n  The new swagger spec written to:\n"
                + outFile.getAbsolutePath()
                + "\n==========================================================================\n");
      }
    }
  }

  @Test
  public void genMd() throws IOException{
    String expectedFlags= expectedFlags();
    String currentFlags=currentFlags();
    if(!expectedFlags.equals(currentFlags)){
      fail("==============================================================================\n"+
      "Flags defination changed.Run $sbt swaggenGen\n"+
      "\n==========================================================================\n");
    }
  }

  private String getSwaggerSpec() throws JsonProcessingException {
    Result result = route(Helpers.fakeRequest("GET", "/docs/dynamic_swagger.json"));
    return sort(contentAsString(result, mat));
  }

  private String expectedFlags(){
    Customer defaultCustomer=ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(defaultCustomer, Users.Role.SuperAdmin);
    String authToken = user.createAuthToken();
    Result result =
              doRequestWithAuthToken("GET", "/api/runtime_config/mutable_key_info", authToken);
    String res="# <u>List of supported Runtime Configuration Flags</u>\n";
    res += "### These are all the public runtime flags in YBA.\n";
    res += "| Name | Key | Scope | Help Text | Data Type |\n";
    res += "| :----: | :----: | :----: | :----: | :----: |\n";
    for(JsonNode j:Json.parse(contentAsString(result))){
      if(!j.get("tags").toString().contains("PUBLIC")) continue;
      res+="| "+j.get("displayName")+" | ";
      res+=j.get("key")+" | ";
      res+=j.get("scope")+" | ";
      res+=j.get("helpTxt")+" | ";
      res+=j.get("dataType").get("name")+" |\n";
    }
    return res;
  }

  private String currentFlags(){
    Path p=Paths.get("").toAbsolutePath().getParent().resolve("RUNTIME-FLAGS.md");
    StringBuilder contentBuilder = new StringBuilder();
    try (Stream<String> stream = Files.lines(p, StandardCharsets.UTF_8)) {
      stream.forEach(S -> contentBuilder.append(S).append("\n"));
    }
    catch(Exception e){
      fail("failed to fetch file. path= "+p.toString());
    }
    return contentBuilder.toString();
  }

  // we will deserialize and serialize back using this mapper so as to generate deterministic
  // sorted diffable/mergable json:
  private static final ObjectMapper SORTED_MAPPER = Json.mapper();

  static {
    SORTED_MAPPER.configure(SerializationFeature.INDENT_OUTPUT, true);
    SORTED_MAPPER.configure(MapperFeature.SORT_PROPERTIES_ALPHABETICALLY, true);
    SORTED_MAPPER.configure(SerializationFeature.ORDER_MAP_ENTRIES_BY_KEYS, true);
  }

  private String sort(String jsonString) throws JsonProcessingException {
    ObjectNode specJsonNode = (ObjectNode) Json.parse(jsonString);
    deleteApiUrl(specJsonNode);
    sortTagsList(specJsonNode);
    final Object obj = SORTED_MAPPER.treeToValue(specJsonNode, Object.class);
    return SORTED_MAPPER.writeValueAsString(obj);
  }

  // Do not hardcode API url
  private void deleteApiUrl(ObjectNode specJsonNode) {
    specJsonNode.remove("host");
    specJsonNode.remove("basePath");
  }

  // This way swagger UI will show tags in sorted order.
  private void sortTagsList(ObjectNode specJsonNode) {
    ArrayNode tagsArrayNode = (ArrayNode) specJsonNode.get("tags");
    List<JsonNode> tagsList = Lists.newArrayList(tagsArrayNode.elements());
    tagsList.sort(Comparator.comparing(tagObject -> tagObject.get("name").asText()));
    ArrayNode sortedTagsArrayNode = Json.newArray().addAll(tagsList);
    specJsonNode.set("tags", sortedTagsArrayNode);
  }

  public static void main(String[] args) throws IOException {
    String expectedSwagger = getCurrentSpec(args[0]);
    excludeDeprecated(args);
    SwaggerGenTest swaggerGenTest = new SwaggerGenTest();
    try {
      swaggerGenTest.startPlay();
      String expectedFlags = swaggerGenTest.expectedFlags();
      String actualFlags = swaggerGenTest.currentFlags();
      if(!actualFlags.equals(expectedFlags)){
        Path p=Paths.get("").toAbsolutePath().getParent().resolve("RUNTIME-FLAGS.md");
        try(FileWriter fw=new FileWriter(p.toFile())){
            fw.write(expectedFlags);
          }
        System.out.println("New flags doc generated...");
      }
      else{
        System.out.println("Runtime flags haven't changed");
      }
      System.out.println("Generating swagger spec:" + Arrays.toString(args));
      String swaggerSpec = swaggerGenTest.getSwaggerSpec();
      if (expectedSwagger.length() == swaggerSpec.length()) {
        // TODO: Fix this: Only length comparison because the json ordering change
        System.out.println("Swagger Specs have not changed");
        return;
      }
      try (FileWriter fileWriter = new FileWriter(new File(args[0]))) {
        // todo only generate on change
        fileWriter.write(swaggerSpec);
        System.out.println("Swagger spec generated");
      }
    } finally {
      swaggerGenTest.stopPlay();
    }
  }

  private static String getCurrentSpec(String arg) {
    String expectedSwagger;
    try (InputStream is = new FileInputStream(arg)) {
      expectedSwagger = IOUtils.toString(is, StandardCharsets.UTF_8);
    } catch (Exception exception) {
      throw new RuntimeException(exception);
    }
    return expectedSwagger;
  }

  private static void excludeDeprecated(String[] args) {
    PlatformModelConverter.excludeYbaDeprecatedOption = "";
    if (args.length > 2 && args[1].equalsIgnoreCase("--exclude_deprecated")) {
      PlatformModelConverter.excludeYbaDeprecatedOption = args[2];
    }
  }
}
