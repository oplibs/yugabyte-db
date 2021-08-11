package com.yugabyte.yw.commissioner.tasks;

import java.util.UUID;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Customer;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.BackupTableParams;
import java.util.List;
import com.yugabyte.yw.commissioner.tasks.DeleteCustomerConfig;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

import com.yugabyte.yw.common.ModelFactory;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import org.junit.Before;

@RunWith(MockitoJUnitRunner.class)
public class DeleteCustomerConfigTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Backup backup;
  private Schedule schedule;
  private CustomerConfig nfsStorageConfig;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    nfsStorageConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST0");
    backup =
        ModelFactory.createBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, nfsStorageConfig.configUUID);
    schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, nfsStorageConfig.configUUID);
  }

  @Test
  public void testDeleteCustomerConfigWithBackups() {
    DeleteCustomerConfig.Params params = new DeleteCustomerConfig.Params();
    params.customerUUID = defaultCustomer.uuid;
    params.configUUID = nfsStorageConfig.configUUID;
    params.isDeleteBackups = true;
    BackupTableParams bParams = backup.getBackupInfo();
    bParams.actionType = BackupTableParams.ActionType.CREATE;
    backup.setBackupInfo(bParams);
    backup.transitionState(Backup.BackupState.Completed);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);
    DeleteCustomerConfig deleteCustomerConfigTask =
        UniverseTaskBase.createTask(DeleteCustomerConfig.class);
    deleteCustomerConfigTask.initialize(params);
    deleteCustomerConfigTask.run();
    verify(mockTableManager, times(1)).deleteBackup(any());
    // Backup state should be DELETED.
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(Backup.BackupState.Deleted, backup.state);
  }

  @Test
  public void testDeleteCustomerConfigWithoutBackups() {
    DeleteCustomerConfig.Params params = new DeleteCustomerConfig.Params();
    params.customerUUID = defaultCustomer.uuid;
    params.configUUID = nfsStorageConfig.configUUID;
    DeleteCustomerConfig deleteCustomerConfigTask =
        UniverseTaskBase.createTask(DeleteCustomerConfig.class);
    deleteCustomerConfigTask.initialize(params);
    deleteCustomerConfigTask.run();
    verify(mockTableManager, times(0)).deleteBackup(any());
  }

  @Test
  public void testDeleteCustomerConfigWithSchedules() {
    DeleteCustomerConfig.Params params = new DeleteCustomerConfig.Params();
    params.customerUUID = defaultCustomer.uuid;
    params.configUUID = nfsStorageConfig.configUUID;
    DeleteCustomerConfig deleteCustomerConfigTask =
        UniverseTaskBase.createTask(DeleteCustomerConfig.class);
    deleteCustomerConfigTask.initialize(params);
    deleteCustomerConfigTask.run();
    schedule = Schedule.getOrBadRequest(schedule.scheduleUUID);
    assertEquals(Schedule.State.Stopped, schedule.getStatus());
    verify(mockTableManager, times(0)).deleteBackup(any());
  }
}
