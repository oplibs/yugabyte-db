/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { Dictionary, groupBy } from 'lodash';
import moment from 'moment';
import { IBackup, Keyspace_Table, RESTORE_ACTION_TYPE, TIME_RANGE_STATE } from '..';
import { ROOT_URL } from '../../../config';
import { BACKUP_API_TYPES, Backup_Options_Type, ITable } from './IBackup';

export function getBackupsList(
  page = 0,
  limit = 10,
  searchText: string,
  timeRange: TIME_RANGE_STATE,
  states: any[],
  sortBy: string,
  direction: string,
  universeUUID?: string,
  storageConfigUUID?: string | null
) {
  const cUUID = localStorage.getItem('customerId');
  const payload = {
    sortBy,
    direction,
    filter: {},
    limit,
    offset: page,
    needTotalCount: true
  };
  if (searchText) {
    payload['filter'] = {
      universeNameList: [searchText]
    };
  }
  if (universeUUID) {
    payload['filter']['universeUUIDList'] = [universeUUID];
  }

  if (storageConfigUUID) {
    payload['filter']['storageConfigUUIDList'] = [storageConfigUUID];
  }

  if (states.length !== 0 && states[0].label !== 'All') {
    payload.filter['states'] = [states[0].value];
  }
  if (timeRange.startTime && timeRange.endTime) {
    payload.filter['dateRangeStart'] = timeRange.startTime.toISOString();
    payload.filter['dateRangeEnd'] = timeRange.endTime.toISOString();
  }
  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/page`, payload);
}

export function restoreEntireBackup(backup: IBackup, values: Record<string, any>) {
  const cUUID = localStorage.getItem('customerId');
  const backupStorageInfoList = values['keyspaces'].map(
    (keyspace: Keyspace_Table, index: number) => {
      return {
        backupType: backup.backupType,
        keyspace: keyspace || backup.responseList[index].keyspace,
        sse: backup.sse,
        storageLocation: backup.responseList[index].storageLocation,
        tableNameList: backup.responseList[index].tablesList
      };
    }
  );
  const payload = {
    actionType: RESTORE_ACTION_TYPE.RESTORE,
    backupStorageInfoList: backupStorageInfoList,
    customerUUID: cUUID,
    universeUUID: values['targetUniverseUUID'].value,
    storageConfigUUID: backup.storageConfigUUID,
    parallelism: values['parallelThreads']
  };
  if (values['kmsConfigUUID']) {
    payload['encryptionAtRestConfig'] = {
      encryptionAtRestEnabled: true,
      kmsConfigUUID: values['kmsConfigUUID'].value
    };
  }
  return axios.post(`${ROOT_URL}/customers/${cUUID}/restore`, payload);
}

export function deleteBackup(backupList: IBackup[]) {
  const cUUID = localStorage.getItem('customerId');
  const backup_data = backupList.map((b) => {
    return {
      backupUUID: b.backupUUID,
      storageConfigUUID: b.storageConfigUUID
    };
  });
  return axios.delete(`${ROOT_URL}/customers/${cUUID}/delete_backups`, {
    data: {
      deleteBackupInfos: backup_data
    }
  });
}

export function cancelBackup(backup: IBackup) {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/${backup.backupUUID}/stop`);
}

export function getKMSConfigs() {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/kms_configs`;
  return axios.get(requestUrl).then((resp) => resp.data);
}

export function createBackup(values: Record<string, any>) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/backups`;

  const backup_type = values['api_type'].value;

  const payload = {
    backupType: backup_type,
    customerUUID: cUUID,
    parallelism: values['parallel_threads'],
    sse: values['storage_config'].name === 'S3',
    storageConfigUUID: values['storage_config'].value,
    timeBeforeDelete: 0,
    universeUUID: values['universeUUID']
  };

  let dbMap: Dictionary<any> = [];

  const filteredTableList = values['tablesList'].filter((t: ITable) => t.tableType === backup_type);

  if (values['db_to_backup'].value === null) {
    // All database/ keyspace selected
    dbMap = groupBy(filteredTableList, 'keySpace');
  } else {
    dbMap = {
      [values['db_to_backup'].value]: filteredTableList.filter(
        (t: ITable) => t.keySpace === values['db_to_backup'].value
      )
    };
  }

  if (
    backup_type === BACKUP_API_TYPES.YCQL &&
    values['backup_tables'] === Backup_Options_Type.CUSTOM
  ) {
    dbMap = groupBy(values['selected_ycql_tables'], 'keySpace');
  }

  payload['keyspaceTableList'] = Object.keys(dbMap).map((keyspace) => {
    if (backup_type === BACKUP_API_TYPES.YSQL) {
      return {
        keyspace
      };
    }
    return {
      keyspace,
      tableNameList: dbMap[keyspace].map((t: ITable) => t.tableName),
      tableUUIDList: dbMap[keyspace].map((t: ITable) => t.tableUUID)
    };
  });

  //Calculate TTL
  if (values['keep_indefinitely']) {
    payload['timeBeforeDelete'] = 0;
  } else {
    payload['timeBeforeDelete'] = moment()
      .add(values['duration_period'], values['duration_type'].value)
      .diff(moment(), 'second');
  }

  return axios.post(requestUrl, payload);
}
