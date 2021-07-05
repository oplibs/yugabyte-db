/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.filters.AlertDefinitionTemplateFilter;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
public class AlertDefinitionTemplateApiFilter {
  String name;
  AlertDefinitionGroup.TargetType targetType;

  public AlertDefinitionTemplateFilter toFilter() {
    AlertDefinitionTemplateFilter.AlertDefinitionTemplateFilterBuilder builder =
        AlertDefinitionTemplateFilter.builder();
    if (name != null) {
      builder.name(name);
    }
    if (targetType != null) {
      builder.targetType(targetType);
    }
    return builder.build();
  }
}
