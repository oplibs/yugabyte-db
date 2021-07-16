// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/** This class will be used by Table controller createMultiTableBackup API. */
@ApiModel(value = "Multi table backup request params", description = "Backup table params")
public class MultiTableBackupRequestParams extends BackupTableParams {

  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUUID;

  @ApiModelProperty(value = "Table UUID List")
  public List<UUID> tableUUIDList = new ArrayList<>();
}
