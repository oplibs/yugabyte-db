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
import type { AuthTokenInfo } from './AuthTokenInfo';
// eslint-disable-next-line no-duplicate-imports
import type { AuthTokenSpec } from './AuthTokenSpec';


/**
 * Auth Token Data
 * @export
 * @interface AuthTokenData
 */
export interface AuthTokenData  {
  /**
   * 
   * @type {AuthTokenSpec}
   * @memberof AuthTokenData
   */
  spec?: AuthTokenSpec;
  /**
   * 
   * @type {AuthTokenInfo}
   * @memberof AuthTokenData
   */
  info?: AuthTokenInfo;
}



