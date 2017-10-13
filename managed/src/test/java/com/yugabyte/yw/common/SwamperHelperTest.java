// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Universe;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.Configuration;
import play.libs.Json;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class SwamperHelperTest extends FakeDBApplication {
  @Mock
  Configuration appConfig;

  @InjectMocks
  SwamperHelper swamperHelper;

  static String SWAMPER_TMP_PATH = "/tmp/swamper/";

  @BeforeClass
  public static void setUp() {
    new File(SWAMPER_TMP_PATH).mkdir();
  }

  @AfterClass
  public static void tearDown() {
    File file = new File(SWAMPER_TMP_PATH);
    file.delete();
  }

  @Test
  public void testWriteUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdaterWithInactiveNodes());

    try {
      swamperHelper.writeUniverseTargetJson(u.universeUUID);
      BufferedReader br = new BufferedReader(new FileReader(
          SWAMPER_TMP_PATH + "yugabyte." + u.universeUUID + ".json"));
      String line;
      StringBuilder sb = new StringBuilder();
      while ((line = br.readLine()) != null) {
        sb.append(line);
      }

      JsonNode targetsJson = Json.parse(sb.toString());
      assertThat(targetsJson.size(), is(equalTo(4)));
      List<String> targetTypes = new ArrayList<>();
      for (SwamperHelper.TargetType t : Arrays.asList(SwamperHelper.TargetType.values())) {
        targetTypes.add(t.toString());
      }
      for (int i = 0; i < targetsJson.size(); ++i) {
        JsonNode target = targetsJson.get(i);
        assertTrue(target.get("targets").isArray());
        assertThat(target.get("targets").size(), equalTo(3));
        assertThat(target.get("targets").toString(), RegexMatcher.matchesRegex("(.*)(|:)([0-9]*)"));
        JsonNode labels = target.get("labels");
        assertThat(labels.get("node_prefix").asText(), equalTo("host"));
        assertTrue(targetTypes.contains(labels.get("export_type").asText().toUpperCase()));
      }
    } catch (Exception e) {
      assertTrue(false);
    }
  }

  @Test
  public void testRemoveUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID universeUUID = UUID.randomUUID();
    String yugabyteFilePath = SWAMPER_TMP_PATH + "yugabyte." + universeUUID + ".json";
    String nodeFilePath = SWAMPER_TMP_PATH + "node." + universeUUID + ".json";
    try {
      new File(yugabyteFilePath).createNewFile();
      new File(nodeFilePath).createNewFile();
    } catch (IOException e) {
      assertTrue(false);
    }
    swamperHelper.removeUniverseTargetJson(universeUUID);
    assertFalse(new File(yugabyteFilePath).exists());
    assertFalse(new File(nodeFilePath).exists());
  }

  @Test(expected = RuntimeException.class)
  public void testUniverseTargetWriteFailure() {
    File dir = new File("/tmp/non-writable");
    dir.mkdir();
    dir.setWritable(false);

    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(dir.getPath());
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    swamperHelper.writeUniverseTargetJson(u.universeUUID);
  }

  public void testUniverseTargetWithoutTargetPath() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn("");
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    swamperHelper.writeUniverseTargetJson(u.universeUUID);
  }
}
