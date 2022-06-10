// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Objects;
import javax.validation.Valid;
import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;
import lombok.extern.jackson.Jacksonized;

public class TableSpaceStructures {

  @ApiModel(description = "Tablespace information")
  @Jacksonized
  @Builder
  @NoArgsConstructor
  @AllArgsConstructor
  public static class TableSpaceInfo {
    @ApiModelProperty(value = "Tablespace Name")
    @NotNull
    @Size(min = 1)
    public String name;

    @ApiModelProperty(value = "numReplicas")
    @Min(value = 1)
    public int numReplicas;

    @Valid
    @ApiModelProperty(value = "placements")
    @NotNull
    @Size(min = 1)
    public List<PlacementBlock> placementBlocks;

    @Override
    public int hashCode() {
      return Objects.hash(name, numReplicas, placementBlocks);
    }

    @Override
    public boolean equals(Object obj) {
      if (this == obj) return true;
      if (obj == null) return false;
      if (getClass() != obj.getClass()) return false;
      TableSpaceInfo other = (TableSpaceInfo) obj;
      return Objects.equals(name, other.name)
          && numReplicas == other.numReplicas
          && CommonUtils.isEqualIgnoringOrder(placementBlocks, other.placementBlocks);
    }
  }

  @EqualsAndHashCode
  @NoArgsConstructor
  public static class PlacementBlock {
    @ApiModelProperty(value = "Cloud")
    @NotNull
    @Size(min = 1)
    public String cloud;

    @ApiModelProperty(value = "Region")
    @NotNull
    @Size(min = 1)
    public String region;

    @ApiModelProperty(value = "Zone")
    @NotNull
    @Size(min = 1)
    public String zone;

    @ApiModelProperty(value = "Minimum replicas")
    @JsonProperty("min_num_replicas")
    @JsonAlias("minNumReplicas")
    @Min(1)
    public int minNumReplicas;

    @ApiModelProperty(value = "Leader preference")
    @JsonInclude(value = JsonInclude.Include.NON_NULL)
    @JsonProperty("leader_preference")
    @JsonAlias("leaderPreference")
    @Min(1)
    public Integer leaderPreference;
  }

  public static class TableSpaceQueryResponse {
    @JsonProperty("spcname")
    public String tableSpaceName;

    @JsonProperty("spcoptions")
    public List<String> tableSpaceOptions;
  }

  static class TableSpaceOptions {
    @JsonProperty("num_replicas")
    public int numReplicas;

    @JsonProperty("placement_blocks")
    public List<PlacementBlock> placementBlocks;
  }

  public static class QueryDistributionAcrossNodesResponse {
    @JsonProperty("calls")
    public Integer calls;

    @JsonProperty("query")
    public String query;
  }
}
