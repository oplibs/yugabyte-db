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


/**
 * Auth Token Info
 * @export
 * @interface AuthTokenInfo
 */
export interface AuthTokenInfo  {
  /**
   * The UUID of the token
   * @type {string}
   * @memberof AuthTokenInfo
   */
  id: string;
  /**
   * Email of the user who issued the jwt
   * @type {string}
   * @memberof AuthTokenInfo
   */
  issuer: string;
  /**
   * 
   * @type {EntityMetadata}
   * @memberof AuthTokenInfo
   */
  metadata?: EntityMetadata;
}



