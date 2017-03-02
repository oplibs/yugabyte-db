// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.models.YugawareProperty;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.error.YAMLException;
import play.Application;
import play.libs.Json;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class ConfigHelperTest extends FakeDBApplication {

  @InjectMocks
  ConfigHelper configHelper;

  @Mock
  Util util;

  @Mock
  Application application;

  private InputStream asYamlStream(Map<String, Object> map) throws IOException {
    Yaml yaml = new Yaml();
    String fileName = createTempFile("file.yml", yaml.dump(map));
    File initialFile = new File(fileName);
    return new FileInputStream(initialFile);
  }

  @Test
  public void testLoadConfigsToDBWithFile() throws IOException {
    Map<String, Object> map = new HashMap();
    map.put("config-1", "foo");
    map.put("config-2", "bar");

    for (ConfigHelper.ConfigType configType: ConfigHelper.ConfigType.values()) {
      when(application.classloader()).thenReturn(ClassLoader.getSystemClassLoader());
      when(application.resourceAsStream(configType.getConfigFile())).thenReturn(asYamlStream(map));
    }
    configHelper.loadConfigsToDB();

    for (ConfigHelper.ConfigType configType: ConfigHelper.ConfigType.values()) {
      assertEquals(map, configHelper.getConfig(configType));
    }
  }

  @Test(expected = YAMLException.class)
  public void testLoadConfigsToDBWithoutFile() {
    when(application.classloader()).thenReturn(ClassLoader.getSystemClassLoader());
    configHelper.loadConfigsToDB();
  }

  @Test
  public void testGetConfigWithData() {
    Map<String, Object> map = new HashMap();
    map.put("foo", "bar");
    ConfigHelper.ConfigType testConfig = ConfigHelper.ConfigType.AWSInstanceTypeMetadata;
    YugawareProperty.addConfigProperty(testConfig.toString(),
        Json.toJson(map), testConfig.getDescription());

    Map<String, Object> data = configHelper.getConfig(testConfig);
    assertThat(data.get("foo"), allOf(notNullValue(), equalTo("bar")));
  }

  @Test
  public void testGetConfigWithoutData() {
    ConfigHelper.ConfigType testConfig = ConfigHelper.ConfigType.AWSInstanceTypeMetadata;
    Map<String, Object> data = configHelper.getConfig(testConfig);
    assertTrue(data.isEmpty());
  }

  @Test
  public void testGetConfigWithNullValue() {
    ConfigHelper.ConfigType testConfig = ConfigHelper.ConfigType.AWSInstanceTypeMetadata;
    YugawareProperty.addConfigProperty(testConfig.toString(), null, testConfig.getDescription());
    Map<String, Object> data = configHelper.getConfig(testConfig);
    assertTrue(data.isEmpty());
  }
}
