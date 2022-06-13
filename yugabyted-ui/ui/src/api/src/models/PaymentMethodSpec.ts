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
import type { PaymentMethodEnum } from './PaymentMethodEnum';


/**
 * Payment method spec
 * @export
 * @interface PaymentMethodSpec
 */
export interface PaymentMethodSpec  {
  /**
   * 
   * @type {string}
   * @memberof PaymentMethodSpec
   */
  payment_method_id: string;
  /**
   * 
   * @type {boolean}
   * @memberof PaymentMethodSpec
   */
  is_default: boolean;
  /**
   * 
   * @type {PaymentMethodEnum}
   * @memberof PaymentMethodSpec
   */
  payment_method_type: PaymentMethodEnum;
}



