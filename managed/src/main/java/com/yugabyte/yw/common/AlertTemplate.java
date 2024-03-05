// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import io.swagger.annotations.ApiModel;

@ApiModel
public enum AlertTemplate {
  REPLICATION_LAG,
  CLOCK_SKEW,
  MEMORY_CONSUMPTION,
  HEALTH_CHECK_ERROR,
  HEALTH_CHECK_NOTIFICATION_ERROR,
  UNIVERSE_METRIC_COLLECTION_FAILURE,
  BACKUP_FAILURE,
  BACKUP_DELETION_FAILURE,
  BACKUP_SCHEDULE_FAILURE,
  INACTIVE_CRON_NODES,
  ALERT_QUERY_FAILED,
  ALERT_CONFIG_WRITING_FAILED,
  ALERT_NOTIFICATION_ERROR,
  ALERT_NOTIFICATION_CHANNEL_ERROR,
  NODE_DOWN,
  NODE_RESTART,
  NODE_CPU_USAGE,
  NODE_DISK_USAGE,
  NODE_SYSTEM_DISK_USAGE,
  NODE_FILE_DESCRIPTORS_USAGE,
  NODE_OOM_KILLS,
  DB_VERSION_MISMATCH,
  DB_INSTANCE_DOWN,
  DB_INSTANCE_RESTART,
  DB_FATAL_LOGS,
  DB_ERROR_LOGS,
  DB_CORE_FILES,
  DB_YSQL_CONNECTION,
  DB_YCQL_CONNECTION,
  DB_REDIS_CONNECTION,
  DB_MEMORY_OVERLOAD,
  DB_COMPACTION_OVERLOAD,
  DB_QUEUES_OVERFLOW,
  DB_DRIVE_FAILURE,
  DB_WRITE_READ_TEST_ERROR,
  NODE_TO_NODE_CA_CERT_EXPIRY,
  NODE_TO_NODE_CERT_EXPIRY,
  CLIENT_TO_NODE_CA_CERT_EXPIRY,
  CLIENT_TO_NODE_CERT_EXPIRY,
  ENCRYPTION_AT_REST_CONFIG_EXPIRY,
  SSH_KEY_EXPIRY,
  SSH_KEY_ROTATION_FAILURE,
  PITR_CONFIG_FAILURE,
  YSQL_OP_AVG_LATENCY,
  YCQL_OP_AVG_LATENCY,
  YSQL_OP_P99_LATENCY,
  YCQL_OP_P99_LATENCY,
  HIGH_NUM_YSQL_CONNECTIONS,
  HIGH_NUM_YCQL_CONNECTIONS,
  HIGH_NUM_YEDIS_CONNECTIONS,
  YSQL_THROUGHPUT,
  YCQL_THROUGHPUT,
  MASTER_LEADER_MISSING,
  MASTER_UNDER_REPLICATED,
  LEADERLESS_TABLETS,
  UNDER_REPLICATED_TABLETS,
  PRIVATE_ACCESS_KEY_STATUS,
  NEW_YSQL_TABLES_ADDED,
  UNIVERSE_OS_UPDATE_REQUIRED,
  DB_YCQL_WEB_SERVER_DOWN,
  DB_YSQL_WEB_SERVER_DOWN,
  INCREASED_REMOTE_BOOTSTRAPS,
  TABLET_SERVER_AVG_READ_LATENCY,
  TABLET_SERVER_AVG_WRITE_LATENCY,
  REACTOR_DELAYS,
  RPC_QUEUE_SIZE,
  LOG_CACHE_SIZE,
  CACHE_MISS;
}
