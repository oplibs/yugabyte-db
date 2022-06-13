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
import type { SubnetDataResponse } from './SubnetDataResponse';
// eslint-disable-next-line no-duplicate-imports
import type { VpcDataResponse } from './VpcDataResponse';


/**
 * Information about cluster network
 * @export
 * @interface NetworkDataResponse
 */
export interface NetworkDataResponse  {
  /**
   * 
   * @type {VpcDataResponse}
   * @memberof NetworkDataResponse
   */
  vpc?: VpcDataResponse;
  /**
   * 
   * @type {SubnetDataResponse[]}
   * @memberof NetworkDataResponse
   */
  subnets?: SubnetDataResponse[];
}



