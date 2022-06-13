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
import type { ActiveClusterRate } from './ActiveClusterRate';
// eslint-disable-next-line no-duplicate-imports
import type { PausedClusterRate } from './PausedClusterRate';


/**
 * Rate info
 * @export
 * @interface RateInfo
 */
export interface RateInfo  {
  /**
   * 
   * @type {ActiveClusterRate}
   * @memberof RateInfo
   */
  active_cluster: ActiveClusterRate;
  /**
   * 
   * @type {PausedClusterRate}
   * @memberof RateInfo
   */
  paused_cluster: PausedClusterRate;
}



