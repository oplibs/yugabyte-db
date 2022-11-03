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
import type { ClusterTablet } from './ClusterTablet';


/**
 *
 * @export
 * @interface ClusterTabletListResponse
 */
export interface ClusterTabletListResponse  {
  /**
   * List of cluster tablets
   * @type {{ [key: string]: ClusterTablet; }}
   * @memberof ClusterTabletListResponse
   */
  data: { [key: string]: ClusterTablet; };
}
