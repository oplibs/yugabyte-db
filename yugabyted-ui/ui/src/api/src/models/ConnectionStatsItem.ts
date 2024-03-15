// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */




/**
 * 
 * @export
 * @interface ConnectionStatsItem
 */
export interface ConnectionStatsItem  {
  /**
   * 
   * @type {string}
   * @memberof ConnectionStatsItem
   *
   * GH #19722 : Structure of Ysql Connection Manager stats
   * have changed.
   */
  pool?: string;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  active_logical_connections: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  queued_logical_connections: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  idle_or_pending_logical_connections: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  active_physical_connections: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  idle_physical_connections: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  avg_wait_time_ns: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  qps: number;
  /**
   * 
   * @type {number}
   * @memberof ConnectionStatsItem
   */
  tps: number;
}



