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
import type { CloudEnum } from './CloudEnum';


/**
 * Cloud deployment information
 * @export
 * @interface CloudInfo
 */
export interface CloudInfo  {
  /**
   * 
   * @type {CloudEnum}
   * @memberof CloudInfo
   */
  code: CloudEnum;
  /**
   * 
   * @type {string}
   * @memberof CloudInfo
   */
  region: string;
}



