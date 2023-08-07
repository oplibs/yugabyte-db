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
 * Model representing a tablet
 * @export
 * @interface ClusterTablet
 */
export interface ClusterTablet  {
  /**
   * 
   * @type {string}
   * @memberof ClusterTablet
   */
  namespace: string;
  /**
   * 
   * @type {string}
   * @memberof ClusterTablet
   */
  table_name: string;
  /**
   * 
   * @type {string}
   * @memberof ClusterTablet
   */
  table_uuid: string;
  /**
   * 
   * @type {string}
   * @memberof ClusterTablet
   */
  tablet_id?: string;
  /**
   * 
   * @type {boolean}
   * @memberof ClusterTablet
   */
  has_leader: boolean;
}



