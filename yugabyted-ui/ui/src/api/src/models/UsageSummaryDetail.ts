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
 * usage summary
 * @export
 * @interface UsageSummaryDetail
 */
export interface UsageSummaryDetail  {
  /**
   * 
   * @type {string}
   * @memberof UsageSummaryDetail
   */
  name: string;
  /**
   * 
   * @type {string}
   * @memberof UsageSummaryDetail
   */
  description?: string;
  /**
   * 
   * @type {string}
   * @memberof UsageSummaryDetail
   */
  unit?: string;
  /**
   * 
   * @type {number}
   * @memberof UsageSummaryDetail
   */
  quantity: number;
  /**
   * 
   * @type {number}
   * @memberof UsageSummaryDetail
   */
  unit_price: number;
  /**
   * 
   * @type {number}
   * @memberof UsageSummaryDetail
   */
  amount: number;
}



