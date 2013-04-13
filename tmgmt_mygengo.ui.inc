<?php

/**
 * @file
 * Please supply a file description.
 */

/**
 * @file
 * Provides Gengo translation plugin controller.
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
      '#title' => t('Gengo API Public key'),
      '#default_value' => $translator->getSetting('api_public_key'),
      '#description' => t('Please enter your Gengo API Public key.'),
    );
    $form['api_private_key'] = array(
      '#type' => 'textfield',
      '#title' => t('Gengo API Private key'),
      '#default_value' => $translator->getSetting('api_private_key'),
      '#description' => t('Please enter your Gengo API Private key.'),
    );
    $form['mygengo_auto_approve'] = array(
      '#type' => 'checkbox',
      '#title' => t('Auto approve'),
      '#default_value' => $translator->getSetting('mygengo_auto_approve'),
      '#description' => t('Check to auto approve translated jobs from Gengo. If this setting is off there is a review process available, however each rejection will result in submission of a new job to Gengo service and therefore additional costs.'),
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
      '#description' => t('Check to use translators from the preferred translators list associated with your Gengo account.'),
    );
    $form['show_remaining_credits_info'] = array(
      '#type' => 'checkbox',
      '#title' => t('Show remaining credit info'),
      '#default_value' => $translator->getSetting('show_remaining_credits_info'),
      '#description' => t('Check to display remaining Gengo credit at the job checkout page.'),
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
    $translator = $job->getTranslator();

    // Set the quality setting from submitted vals - we need this for quote as
    // repetitive change of Quality select will not update the job settings.
    if (isset($form_state['values']['settings']['quality'])) {
      $job->settings['quality'] = $form_state['values']['settings']['quality'];
    }

    // In case quality has not been set yet, init it to default.
    if (empty($job->settings['quality'])) {
      $job->settings['quality'] = 'standard';
    }

    $tier_names = array(
      'machine' => t('Machine'),
      'standard' => t('Standard'),
      'pro' => t('Business'),
      'ultra' => t('Ultra'),
    );

    $available_tiers = array();
    // Machine translation is always available.
    $available_tiers['machine'] = $tier_names['machine'];

    // Build quality options based on returned language pairs data. This
    // approach will filter out tiers that are not supported for current
    // source/target lang pair.
    $gengo_language_pairs = $plugin->getGengoLanguagePairs($translator, $job->source_language, $job->target_language);
    foreach ($gengo_language_pairs as $tier) {
      $available_tiers[$tier->tier] = t('@tier (@cost @currency per word)', array(
        '@tier' => empty($tier_names[$tier->tier]) ? $tier->tier : $tier_names[$tier->tier],
        '@cost' => number_format($tier->unit_price, 2),
        '@currency' => $tier->currency,
      ));
    }

    // Gengo service does not support ultra quality for grouped jobs and we
    // send all jobs as grouped. We need to wait until gengo will support
    // grouped jobs for ultra quality as well.
    unset($available_tiers['ultra']);

    $settings['quality'] = array(
      '#type' => 'select',
      '#title' => t('Quality'),
      '#options' => $available_tiers,
      '#default_value' => $job->settings['quality'],
      '#description' => t('Choose the level of quality for this translation job.'),
      '#ajax' => array(
        'callback' => 'tmgmt_ui_ajax_callback_translator_select',
        'wrapper' => 'tmgmt-ui-translator-settings',
      ),
    );

    if ($job->settings['quality'] == 'machine') {
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
      if ($translator->getSetting('show_remaining_credits_info')) {
        $settings['quote']['remaining_credits'] = array(
          '#type' => 'item',
          '#title' => t('Remaining Credits'),
          '#markup' => $plugin->getRemainingCredit($job),
        );
      }
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

  /**
   * Implements TMGMTTranslatorUIControllerInterface::checkoutInfo().
   */
  public function checkoutInfo(TMGMTJob $job) {
    $form = array();

    if ($job->isActive()) {
      $form['actions']['poll'] = array(
        '#type' => 'submit',
        '#value' => t('Poll translations'),
        '#submit' => array('_tmgmt_mygengo_poll_submit'),
        '#weight' => -10,
      );
    }

    return $form;
  }
}
