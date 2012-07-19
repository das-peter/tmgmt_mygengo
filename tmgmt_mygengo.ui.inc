<?php

/**
 * @file
 * Please supply a file description.
 */

/**
 * @file
 * Provides myGengo translation plugin controller.
 */
class TMGMTMyGengoTranslatorUIController extends TMGMTDefaultTranslatorUIController {

  /**
   * Implements TMGMTTranslatorUIControllerInterface::reviewForm().
   */
  public function reviewForm($form, &$form_state, TMGMTJobItem $item) {
    return $form;
  }

  /**
   * Overrides TMGMTDefaultTranslatorUIController::pluginSettingsForm().
   */
  public function pluginSettingsForm($form, &$form_state, TMGMTTranslator $translator, $busy = FALSE) {

    $form['api_public_key'] = array(
      '#type' => 'textfield',
      '#title' => t('myGengo API Public key'),
      '#default_value' => $translator->getSetting('api_public_key'),
      '#description' => t('Please enter yout myGengo API Public key.'),
    );
    $form['api_private_key'] = array(
      '#type' => 'textfield',
      '#title' => t('myGengo API Private key'),
      '#default_value' => $translator->getSetting('api_private_key'),
      '#description' => t('Please enter your myGengo API Private key.'),
    );
    $form['mygengo_auto_approve'] = array(
      '#type' => 'checkbox',
      '#title' => t('Auto approve'),
      '#default_value' => $translator->getSetting('mygengo_auto_approve'),
      '#description' => t('Check to auto approve translated jobs from myGengo. If this setting is off there is a review process available, however each rejection will result in submission of a new job to myGengo service and therefore additional costs.'),
      '#prefix' => '<div class="mygengo-auto-approve">',
      '#suffix' => '</div>',
    );
    $form['use_sandbox'] = array(
      '#type' => 'checkbox',
      '#title' => t('Use the sandbox'),
      '#default_value' => $translator->getSetting('use_sandbox'),
      '#description' => t('Check to use the testing environment. If you use sandbox auto approve has to be turned on as only with this setting you will be able to go through the whole translation workflow.'),
      '#prefix' => '<div class="mygengo-use-sandbox">',
      '#suffix' => '</div>',
    );

    return parent::pluginSettingsForm($form, $form_state, $translator, $busy);
  }

  /**
   * Overrides TMGMTDefaultTranslatorUIController::checkoutSettingsForm().
   */
  public function checkoutSettingsForm($form, &$form_state, TMGMTJob $job) {
    /**
     * @var TMGMTMyGengoTranslatorPluginController $plugin
     */
    $plugin = tmgmt_translator_plugin_controller($this->pluginType);

    $job->settings['quality'] = !empty($form_state['values']['settings']['quality']) ? $form_state['values']['settings']['quality'] : 'machine';
    $gengo_jobs = $plugin->gengoSendJob($job);

    $sum_credits = 0;
    $highest_eta = '';
    $sum_unit_count = 0;

    foreach ($gengo_jobs as $mygengo_job) {
      $sum_unit_count = $sum_unit_count + $mygengo_job->unit_count;
      $sum_credits = $sum_credits + $mygengo_job->credits;
      $highest_eta = ($mygengo_job->eta > $highest_eta) ? $mygengo_job->eta : $highest_eta;
    }

    $settings['word_count'] = array(
      '#type' => 'item',
      '#title' => t('Word count'),
      '#markup' => $sum_unit_count,
    );
    $settings['quality'] = array(
      '#type' => 'select',
      '#title' => t('Quality'),
      '#options' => array(
        'machine' => t('Machine'),
        'standard' => t('Standard'),
        'pro' => t('Pro'),
        'ultra' => t('Ultra'),
      ),
      '#description' => t('With which Quality you want to translate?'),
      '#ajax' => array(
        'callback' => 'tmgmt_mygengo_quote',
        'wrapper' => 'mygengo-quote',
      ),
    );
    $settings['quote'] = array(
      '#type' => 'fieldset',
      '#title' => t('Quote'),
      '#prefix' => '<div id="mygengo-quote">',
      '#suffix' => '</div>',
    );
    $settings['quote']['needed_credits'] = array(
      '#type' => 'item',
      '#title' => t('Needed Credits'),
      '#markup' => $sum_credits,
    );
    $settings['quote']['eta'] = array(
      '#type' => 'item',
      '#title' => t('ETA'),
      '#markup' => format_date(mktime() + $highest_eta, "long"),
    );
    return $settings;
  }
}
