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
import type { GlobalRateCardMapOverageRate } from './GlobalRateCardMapOverageRate';


/**
 * Global rate card map for type PROD or NON_PROD
 * @export
 * @interface GlobalRateCardMap
 */
export interface GlobalRateCardMap  {
  /**
   * Base product rate
   * @type {number}
   * @memberof GlobalRateCardMap
   */
  base_product_rate: number;
  /**
   * 
   * @type {GlobalRateCardMapOverageRate}
   * @memberof GlobalRateCardMap
   */
  overage_rate: GlobalRateCardMapOverageRate;
  /**
   * Burstable vCPU rate
   * @type {number}
   * @memberof GlobalRateCardMap
   */
  burstable_vcpu_rate?: number;
  /**
   * Paused cluster disk storage rate
   * @type {number}
   * @memberof GlobalRateCardMap
   */
  paused_cluster_disk_storage_rate?: number;
  /**
   * Describes whether the rate is for prod or non-prod
   * @type {string}
   * @memberof GlobalRateCardMap
   */
  rate_for: GlobalRateCardMapRateForEnum;
}

/**
* @export
* @enum {string}
*/
export enum GlobalRateCardMapRateForEnum {
  Prod = 'PROD',
  NonProd = 'NON_PROD'
}



