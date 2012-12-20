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

    $form['#attached']['js'][] = drupal_get_path('module', 'tmgmt_mygengo') . '/js/tmgmt_mygengo.toggle_auto_approve.js';

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
    $form['use_preferred'] = array(
      '#type' => 'checkbox',
      '#title' => t('Use preferred translators'),
      '#default_value' => $translator->getSetting('use_preferred'),
      '#description' => t('Check to use translators from the preferred translators list associated with your MyGengo account.'),
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

    // Set the quality setting from submitted vals - we need this for quote as
    // repetitive change of Quality select will not update the job settings.
    if (isset($form_state['values']['settings']['quality'])) {
      $job->settings['quality'] = $form_state['values']['settings']['quality'];
    }

    // In case quality has not been set yet, init it to default.
    if (empty($job->settings['quality'])) {
      $job->settings['quality'] = 'standard';
    }

    $tier_names = $options = array(
      'standard' => t('Standard'),
      'pro' => t('Business'),
      'ultra' => t('Ultra'),
    );
    $gengo_language_pairs = $plugin->getGengoLanguagePairs($job->getTranslator(), $job->source_language, $job->target_language);
    $available_tiers = array();

    // Build quality options based on returned language pairs data. This
    // approach will filter out tiers that are not supported for current
    // source/target lang pair.
    foreach ($gengo_language_pairs as $tier) {
      $available_tiers[$tier->tier] = $options[$tier->tier] =
        t('@tier (@cost @currency per word)', array(
          '@tier' => $tier_names[$tier->tier],
          '@cost' => number_format($tier->unit_price, 2),
          '@currency' => $tier->currency,
        ));
    }
    $options = array_intersect_key($available_tiers, $options);
    $options = array_merge(array('machine' => t('Machine')), $options);

    $settings['quality'] = array(
      '#type' => 'select',
      '#title' => t('Quality'),
      '#options' => $options,
      '#default_value' => $job->settings['quality'],
      '#description' => t('Choose the level of quality for this translation job.'),
      '#ajax' => array(
        'callback' => 'tmgmt_ui_ajax_callback_translator_select',
        'wrapper' => 'tmgmt-ui-translator-settings',
      ),
    );

    if ($job->settings['quality'] == 'nonprofit') {
      return $settings;
    }

    $quote = array();

    try {
      $quote = $plugin->getQuote($job);
    }
    catch (TMGMTException $e) {
      $err_msg = t('Unable to get a quote from Gengo with error: !error', array('!error' => $e->getMessage()));
    }

    $settings['quote'] = array(
      '#type' => 'fieldset',
      '#title' => t('Quote'),
    );

    if (!empty($err_msg)) {
      $settings['quote']['error_msg'] = array(
        '#markup' => '<div class="messages error">' . $err_msg . '</div>'
      );
    }
    else {
      $settings['quote']['word_count'] = array(
        '#type' => 'item',
        '#title' => t('Word count'),
        '#markup' => isset($quote['sum_word_count']) ? $quote['sum_word_count'] : t('unknown'),
      );
      $settings['quote']['needed_credits'] = array(
        '#type' => 'item',
        '#title' => t('Needed Credits'),
        '#markup' => number_format($quote['sum_credits'], 2) . ' ' . $quote['currency'],
      );
      $settings['quote']['eta'] = array(
        '#type' => 'item',
        '#title' => t('ETA'),
        '#markup' => format_date(time() + $quote['highest_eta'], "long"),
      );
    }

    $settings['comment'] = array(
      '#type' => 'textarea',
      '#title' => t('Instructions'),
      '#description' => t('You can provide a set of instructions so that the translator will better understand your requirements.'),
      '#default_value' => !empty($job->settings['comment']) ? $job->settings['comment'] : NULL,
    );

    return $settings;
  }
}
