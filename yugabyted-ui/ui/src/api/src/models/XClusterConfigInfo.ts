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


// eslint-disable-next-line no-duplicate-imports
import type { EntityMetadata } from './EntityMetadata';
// eslint-disable-next-line no-duplicate-imports
import type { XClusterConfigStateEnum } from './XClusterConfigStateEnum';


/**
 * XCluster Config Information
 * @export
 * @interface XClusterConfigInfo
 */
export interface XClusterConfigInfo  {
  /**
   * The UUID of the XCluster Config
   * @type {string}
   * @memberof XClusterConfigInfo
   */
  id: string;
  /**
   * 
   * @type {XClusterConfigStateEnum}
   * @memberof XClusterConfigInfo
   */
  state: XClusterConfigStateEnum;
  /**
   * 
   * @type {EntityMetadata}
   * @memberof XClusterConfigInfo
   */
  metadata: EntityMetadata;
}



