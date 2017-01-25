// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import org.junit.Test;
import org.mindrot.jbcrypt.BCrypt;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Customer;

import javax.persistence.PersistenceException;
import java.util.List;
import static org.junit.Assert.*;

public class CustomerTest extends FakeDBApplication {

  @Test
  public void testCreate() {
    Customer customer = Customer.create("Test Customer", "foo@bar.com", "password");
    customer.save();
    assertNotNull(customer.uuid);
    assertEquals("foo@bar.com", customer.getEmail());
    assertEquals("Test Customer", customer.name);
    assertNotNull(customer.creationDate);
    assertTrue(BCrypt.checkpw("password", customer.passwordHash));
  }

  @Test(expected = PersistenceException.class)
  public void testCreateWithDuplicateEmail() {
    Customer c1 = Customer.create("C1", "foo@foo.com", "password");
    c1.save();
    Customer c2 = Customer.create("C2", "foo@foo.com", "password");
    c2.save();
  }

  @Test
  public void testCreateValidateUniqueIDs() {
    Customer c1 = Customer.create("C1", "foo1@foo.com", "password");
    c1.save();
    Customer c2 = Customer.create("C2", "foo2@foo.com", "password");
    c2.save();
    assertNotEquals(c1.getCustomerId(), c2.getCustomerId());
    assertTrue(c2.getCustomerId() > c1.getCustomerId());
    assertNotEquals(c1.uuid, c2.uuid);
  }

  @Test
  public void findAll() {
    Customer c1 = Customer.create("C1", "foo@foo.com", "password");
    c1.save();
    Customer c2 = Customer.create("C2", "bar@foo.com", "password");
    c2.save();

    List<Customer> customerList = Customer.find.all();

    assertEquals(2, customerList.size());
  }

  @Test
  public void authenticateWithEmailAndValidPassword() {
    Customer c = Customer.create("C1", "foo@foo.com", "password");
    c.save();
    Customer authCust = Customer.authWithPassword("foo@foo.com", "password");
    assertEquals(authCust.uuid, c.uuid);
  }

  @Test
  public void authenticateWithEmailAndInvalidPassword() {
    Customer c = Customer.create("C1", "foo@foo.com", "password");
    c.save();
    Customer authCust = Customer.authWithPassword("foo@foo.com", "password1");
    assertNull(authCust);
  }

  @Test
  public void testCreateAuthToken() {
    Customer c = Customer.create("C1", "foo@foo.com", "password");
    c.save();

    assertNotNull(c.uuid);

    String authToken = c.createAuthToken();
    assertNotNull(authToken);
    assertNotNull(c.getAuthTokenIssueDate());

    Customer authCust = Customer.authWithToken(authToken);
    assertEquals(authCust.uuid, c.uuid);
  }
  @Test
  public void testAuthTokenExpiry() {
    Customer c1 = Customer.create("C1", "foo@foo.com", "password");
    c1.save();
    assertNotNull(c1.uuid);
    String authTokenOld = c1.createAuthToken();
    assertNotNull(authTokenOld);
    assertNotNull(c1.getAuthTokenIssueDate());
    Customer c2 = Customer.get(c1.uuid);
    String authTokenNew = c2.createAuthToken();
    assertEquals(authTokenNew, authTokenOld);
  }

  @Test
  public void testDeleteAuthToken() {
    Customer c = Customer.create("C1", "foo@foo.com", "password");
    c.save();

    assertNotNull(c.uuid);

    String authToken = c.createAuthToken();
    assertNotNull(authToken);
    assertNotNull(c.getAuthTokenIssueDate());

    Customer fetchCust = Customer.find.where().eq("uuid", c.uuid).findUnique();
    fetchCust.deleteAuthToken();

    fetchCust = Customer.find.where().eq("uuid", c.uuid).findUnique();
    assertNull(fetchCust.getAuthTokenIssueDate());

    Customer authCust = Customer.authWithToken(authToken);
    assertNull(authCust);
  }

  @Test(expected=javax.persistence.PersistenceException.class)
  public void testInvalidCreate() {
    Customer c = Customer.create(null, "foo@bar.com", "password");
    c.save();
  }
}
