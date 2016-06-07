// Copyright (c) Yugabyte, Inc.

package forms;

import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
public class RegisterFormData {
  @Constraints.Required()
  @Constraints.Email
  @Constraints.MinLength(5)
  public String email;

  @Constraints.Required()
  @Constraints.MinLength(6)
  public String password;

  @Constraints.Required()
  @Constraints.MinLength(3)
  public String name;
}
