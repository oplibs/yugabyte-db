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
 * @interface AccountQuotaPaidTier
 */
export interface AccountQuotaPaidTier  {
  /**
   * Maximum number of nodes allowed for a cluster
   * @type {number}
   * @memberof AccountQuotaPaidTier
   */
  max_num_nodes_per_cluster: number;
  /**
   * Maximum number of vcpus allowed for a cluster
   * @type {number}
   * @memberof AccountQuotaPaidTier
   */
  max_num_vcpus_per_cluster: number;
}



