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
import type { NodeDataCloudInfo } from './NodeDataCloudInfo';
// eslint-disable-next-line no-duplicate-imports
import type { NodeDataMetrics } from './NodeDataMetrics';


/**
 * Node data
 * @export
 * @interface NodeData
 */
export interface NodeData  {
  /**
   * 
   * @type {string}
   * @memberof NodeData
   */
  name: string;
  /**
   * 
   * @type {string}
   * @memberof NodeData
   */
  host: string;
  /**
   * 
   * @type {boolean}
   * @memberof NodeData
   */
  is_node_up: boolean;
  /**
   * 
   * @type {boolean}
   * @memberof NodeData
   */
  is_master: boolean;
  /**
   * 
   * @type {boolean}
   * @memberof NodeData
   */
  is_tserver: boolean;
  /**
   * 
   * @type {boolean}
   * @memberof NodeData
   */
  is_read_replica: boolean;
  /**
   * 
   * @type {number}
   * @memberof NodeData
   */
  preference_order?: number;
  /**
   * 
   * @type {boolean}
   * @memberof NodeData
   */
  is_master_up: boolean;
  /**
   * 
   * @type {boolean}
   * @memberof NodeData
   */
  is_bootstrapping: boolean;
  /**
   * 
   * @type {NodeDataMetrics}
   * @memberof NodeData
   */
  metrics: NodeDataMetrics;
  /**
   * 
   * @type {NodeDataCloudInfo}
   * @memberof NodeData
   */
  cloud_info: NodeDataCloudInfo;
  /**
   * 
   * @type {string}
   * @memberof NodeData
   */
  software_version: string;
}



