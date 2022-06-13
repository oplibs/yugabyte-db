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
import type { RuntimeConfigSpec } from './RuntimeConfigSpec';


/**
 * Config key-value pairs for runtime config update
 * @export
 * @interface RuntimeConfigUpdateRequest
 */
export interface RuntimeConfigUpdateRequest  {
  /**
   * 
   * @type {RuntimeConfigSpec[]}
   * @memberof RuntimeConfigUpdateRequest
   */
  configs?: RuntimeConfigSpec[];
}



