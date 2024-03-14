package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import java.util.List;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ReleasesTest extends FakeDBApplication {
  @Test
  public void testCreateAndGet() {
    Release createdRelease = Release.create(UUID.randomUUID(), "1.2.3", "LTS");
    Release foundRelease = Release.get(createdRelease.getReleaseUUID());
    assertEquals(foundRelease.getReleaseUUID(), createdRelease.getReleaseUUID());
    assertEquals(foundRelease.getVersion(), createdRelease.getVersion());
    assertEquals(foundRelease.getReleaseType(), createdRelease.getReleaseType());
  }

  @Test(expected = PlatformServiceException.class)
  public void testBadRequest() {
    Release.getOrBadRequest(UUID.randomUUID());
  }

  @Test
  public void testAddArtifact() {
    Release release = Release.create(UUID.randomUUID(), "1.2.3", "LTS");
    ReleaseArtifact artifact =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, "file_url");
    release.addArtifact(artifact);
    assertEquals(artifact.getReleaseUUID(), release.getReleaseUUID());
    ReleaseArtifact newArtifact = ReleaseArtifact.get(artifact.getArtifactUUID());
    assertEquals(newArtifact.getReleaseUUID(), release.getReleaseUUID());
  }

  @Test
  public void testGetArtifacts() {
    Release release = Release.create(UUID.randomUUID(), "1.2.3", "LTS");
    ReleaseArtifact art1 =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, "file_url");
    ReleaseArtifact art2 =
        ReleaseArtifact.create(
            "sha257",
            ReleaseArtifact.Platform.LINUX,
            PublicCloudConstants.Architecture.x86_64,
            "file_urls");
    release.addArtifact(art1);
    release.addArtifact(art2);
    List<ReleaseArtifact> artifacts = release.getArtifacts();
    assertEquals(artifacts.size(), 2);
  }

  @Test
  public void testGetForArtifactType() {
    Release release1 = Release.create(UUID.randomUUID(), "1.2.3", "LTS");
    Release release2 = Release.create(UUID.randomUUID(), "1.2.4", "LTS");
    ReleaseArtifact art1 =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, "file_url");
    ReleaseArtifact art2 =
        ReleaseArtifact.create(
            "sha257",
            ReleaseArtifact.Platform.LINUX,
            PublicCloudConstants.Architecture.x86_64,
            "file_urls");
    release1.addArtifact(art1);
    release2.addArtifact(art2);
    List<Release> result1 =
        Release.getAllWithArtifactType(ReleaseArtifact.Platform.KUBERNETES, null);
    List<Release> result2 =
        Release.getAllWithArtifactType(
            ReleaseArtifact.Platform.LINUX, PublicCloudConstants.Architecture.x86_64);
    assertEquals(1, result1.size());
    assertEquals(release1.getReleaseUUID(), result1.get(0).getReleaseUUID());
    assertEquals(1, result2.size());
    assertEquals(release2.getReleaseUUID(), result2.get(0).getReleaseUUID());
  }
}
