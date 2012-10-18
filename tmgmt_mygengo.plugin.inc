<?php

/**
 * @file
 * Provides myGengo translation plugin controller.
 */

/**
 * myGengo translation plugin controller.
 */
class TMGMTMyGengoTranslatorPluginController extends TMGMTDefaultTranslatorPluginController implements TMGMTTranslatorRejectDataItem {

  public $debug = TRUE;

  /**
   * Translation service URL.
   *
   * @var string
   */
  protected $apiURL = 'http://api.mygengo.com';

  /**
   * Translation sandbox service URL.
   * @var string
   */
  protected $sandboxURL = 'http://api.sandbox.mygengo.com';

  /**
   * Translation service API version.
   *
   * @var string
   */
  protected $apiVersion = 'v2';

  /**
   * Tels the send job logic to send the job item only with single data item.
   *
   * @var string
   */
  protected $singleDataItemKey;

  /**
   * Internal mock service URL used by tests.
   *
   * @var string
   */
  public $mockServiceURL = 'tmgmt_mygengo_mock';

  /**
   * If set it will be sent by job post action as a comment.
   *
   * @var string
   */
  protected $serviceComment;

  /**
   * Mapping table of Drupal lang codes to Gengo lang codes.
   *
   * @var array
   */
  protected $langCodesMapping = array(
    'zh-hans' => 'zh',
  );

  protected $language_pairs;


  /**
   * Sets comment to be sent to gengo service with job post request.
   *
   * @param string $comment
   */
  public function setServiceComment($comment) {
    $this->serviceComment = check_plain(trim($comment));
  }

  /**
   * Builds and gets the service url.
   *
   * @param TMGMTTranslator $translator
   *
   * @return string
   *   Service url.
   */
  public function getServiceUrl(TMGMTTranslator $translator) {

    // Allow to override the service url for test purpose.
    if ($translator->getSetting('gengo_service_url') != NULL) {
      return $translator->getSetting('gengo_service_url') . '/' . $this->apiVersion;
    }

    $url = NULL;

    if ($translator->getSetting('use_sandbox')) {
      $url = $this->sandboxURL . '/' . $this->apiVersion;
    }
    else {
      $url = $this->apiURL . '/' . $this->apiVersion;
    }

    return $url;
  }

  /**
   * Implements TMGMTTranslatorRejectDataItem::rejectDataItem()
   */
  public function rejectDataItem(TMGMTJobItem $job_item, array $key, array $values = NULL) {
    $job = $job_item->getJob();
    $data_item = $job_item->getData($key);

    // Add comment into data item.
    $this->setServiceComment($values['comment']);

    array_unshift($key, $job_item->tjiid);

    // Request translation for only the rejected data item.
    $this->singleDataItemKey = implode('][', $key);
    $this->requestTranslation($job);

    $job_item->addMessage('Job data item %label has been resubmitted to myGengo translate service.', array('%label' => tmgmt_data_item_label($data_item)));

    return TRUE;
  }

  /**
   * Implements TMGMTTranslatorRejectDataItem::rejectForm()
   */
  public function rejectForm($form, &$form_state) {
    $form['message'] = array(
      '#markup' => '<div class="messages warning">' .
          t('By rejecting this item you will submit a new translate job to the myGengo translate service which will result in additional costs.') . '</div>',
    );
    $form['comment'] = array(
      '#type' => 'textarea',
      '#title' => t('Rejection comment'),
      '#description' => t('Provide a brief explanation that you actually rejected previous translation and state your reasons.'),
    );
    return $form;
  }

  /**
   * Builds, signs and fires a request to myGengo's API.
   *
   * Authentication rituals are documented here:
   * http://mygengo.com/api/developer-docs/authentication/
   *
   * It should be noted that the authentication ritual listed there is
   * different from the one here (v1 vs v1.1, respectively). v1.1 doesn't
   * require signing on the entire data structure, just a timestamp and keys.
   *
   * @param TMGMTTranslator $translator
   * @param $endpoint
   * @param $method
   * @param array $data
   *
   * @return object
   * @throws TMGMTException
   */
  public function requestFromGengo(TMGMTTranslator $translator, $endpoint, $method, $data = array()) {
    $options = array(
      'headers' => array(
        'User-Agent' => 'Drupal myGengo Translation Interface v0.1',
        'Accept' => 'application/json'
      ),
      'method' => $method,
    );

    $timestamp = gmdate('U');

    $url = $this->getServiceUrl($translator) . '/' . $endpoint;

    if ($method == 'GET' || $method == 'DELETE') {
      $query = array_merge(array(
        'api_key' => $translator->getSetting('api_public_key'),
        'api_sig' => hash_hmac('sha1', $timestamp, $translator->getSetting('api_private_key')),
        'ts' => $timestamp
      ), $data);

      $url = url($url, array('query' => $query, 'absolute' => TRUE));
      $response = drupal_http_request($url, $options);
    }
    else {
      $options['headers']['Content-Type'] = 'application/x-www-form-urlencoded';
      $options['data'] = drupal_http_build_query(array(
        'api_key' => $translator->getSetting('api_public_key'),
        'api_sig' => hash_hmac('sha1', $timestamp, $translator->getSetting('api_private_key')),
        'ts' => $timestamp,
        'data' => json_encode($data)
      ));

      $url = url($url, array('absolute' => TRUE));
      $response = drupal_http_request($url, $options);
    }

    if ($this->debug == TRUE) {
      watchdog('tmgmt_mygengo', "Sending request to gengo at @url with data @data\n\nResponse: @response", array(
        '@url' => $url,
        '@data' => var_export($options, TRUE),
        '@response' => var_export($response, TRUE),
      ), WATCHDOG_DEBUG);
    }

    if ($response->code != 200) {
      throw new TMGMTException('Unable to connect to myGengo service due to following error: @error at @url',
        array('@error' => $response->error, '@url' => $url));
    }

    $results = json_decode($response->data);

    if ($results->opstat == 'ok' && isset($results->response)) {
      return $results->response;
    }

    // If we get to this point it is definitely an error state.
    // Get only the first error as we cannot effectively handle multiple error
    // states. It is highly probable that the error would have been the same
    // anyway.
    $gengo_err = reset($results->err);
    $gengo_err = array_shift($gengo_err);

    throw new TMGMTException('MyGengo service returned following error: <strong>@error</strong>', array('@error' => $gengo_err->msg), $gengo_err->code);
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::isAvailable().
   */
  public function isAvailable(TMGMTTranslator $translator) {
    if ($translator->getSetting('api_public_key') && $translator->getSetting('api_private_key')) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::requestTranslation().
   *
   * Here we will actually query source and get translations.
   */
  public function requestTranslation(TMGMTJob $job) {

    try {

      // Check if we have comment from user input and if yes, set it to be sent.
      if (!empty($job->settings['comment'])) {
        $this->setServiceComment($job->settings['comment']);
      }

      foreach ($this->gengoSendJob($job) as $key => $response_job) {

        // Duplicate content has been submitted.
        if (isset($response_job->duplicate)) {
          // TODO - we need to handle duplicates in some way as client will not
          // receive translation for this item.
          continue;
        }

        if ($this->debug) {
          watchdog('tmgmt_mygengo', 'Job submitted. Received response job @response',
            array('@response' => var_export($response_job, TRUE)), WATCHDOG_DEBUG);
        }

        // For machine translations the job is not wrapped in another object
        // however for human translations it is. So try to cope with this
        // gengo system variety.
        if (!isset($response_job->custom_data)) {
          $response_job = reset($response_job);
        }

        // We might already get translated data, so run translation save
        // to capture it.
        // Note that the key here is used the array offset as in the case of
        // receiving an existing translation custom data will not get updated
        // either and the old value is sent.
        $key = array_slice(tmgmt_ensure_keys_array($key), 1);
        $this->saveTranslation($job, $key, $response_job);

        // Create mapping record for gengo job.
        $mapping = array(
          'tjid' => $job->tjid,
          'gjid' => $response_job->job_id,
          'data_item_key' => tmgmt_ensure_keys_string($key),
        );
        drupal_write_record('tmgmt_mygengo_mappings', $mapping);
      }

      $job->submitted(t('Job has been submitted.'));
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e);
      $job->rejected('Job has been rejected with following error: !error',
        array('!error' => $e->getMessage()), 'error');
    }
  }

  /**
   * Receives and stores a translation returned by myGengo.
   */
  public function saveTranslation(TMGMTJob $job, $keys, $data) {
    // Try to add translation for both statuses - approved and available.
    // In case a translation is not new or not in state pending the
    // translation will not be updated anyway. This is handled in
    // TMGMTJobItem::addTranslatedDataRecursive().
    if ($data->status == 'approved' || $data->status == 'available') {
      $keys = tmgmt_ensure_keys_array($keys);
      $job->addTranslatedData(array('#text' => $data->body_tgt), $keys);
    }
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::getSupportedTargetLanguages().
   */
  public function getSupportedTargetLanguages(TMGMTTranslator $translator, $source_language) {
    $results = array();

    try {
      $this->language_pairs = $this->requestFromGengo($translator, 'translate/service/language_pairs',
        'GET', array('lc_src' => $this->mapToGengoLangCode($source_language)));

      foreach ($this->language_pairs as $target) {
        $results[$target->lc_tgt] = $target->lc_tgt;
      }
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e);
    }

    return $results;
  }

  /**
   * Helper method to get unit price for source/target languages and tier.
   *
   * @param TMGMTTranslator $translator
   * @param $source_language
   * @param $target_language
   * @param $tier
   *
   * @return mixed
   *   In case of success a double value.
   *   In case of failure a boolean FALSE.
   */
  public function getUnitPrice(TMGMTTranslator $translator, $source_language, $target_language, $tier) {

    // Send request only in case we did not call getSupportedTargetLanguages()
    // prior to calling this method.
    if (empty($this->language_pairs)) {
      try {
        $this->language_pairs = $this->requestFromGengo($translator, 'translate/service/language_pairs',
          'GET', array('lc_src' => $this->mapToGengoLangCode($source_language)));
      }
      catch (TMGMTException $e) {
        watchdog_exception('tmgmt', $e);
        return FALSE;
      }
    }

    foreach ($this->language_pairs as $target) {
      if (
        $target->lc_tgt == $this->mapToGengoLangCode($target_language) &&
        $target->lc_src == $this->mapToGengoLangCode($source_language) &&
        $target->tier == $tier
      ) {
        return $target->unit_price;
      }
    }

    return FALSE;
  }

  /**
   * Fetches gengo job from the gengo service.
   *
   * @param TMGMTTranslator $translator
   * @param $gengo_job_id
   *   External job id.
   *
   * @return array
   *   Request array returned by requestFromGengo().
   */
  protected function gengoFetchJob(TMGMTTranslator $translator, $gengo_job_id) {
    return $this->requestFromGengo($translator, 'translate/job/' . $gengo_job_id, 'GET');
  }

  public function canTranslate(TMGMTTranslator $translator, TMGMTJob $job) {

    $target_lang = $this->mapToGengoLangCode($job->target_language);
    $source_lang = $this->mapToGengoLangCode($job->source_language);

    if ($this->isAvailable($translator) && array_key_exists($target_lang, $translator->getSupportedTargetLanguages($source_lang))) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Approves job at gengo side.
   *
   * @param TMGMTTranslator $translator
   * @param $gengo_job_id
   *   External job id.
   * @param array $data
   *   Data to be submitted to gengo services.
   *
   * @return array
   *   Request array returned by requestFromGengo().
   */
  protected function gengoApproveJob(TMGMTTranslator $translator, $gengo_job_id, $data = array()) {

    $data += array('action' => 'approve');

    return $this->requestFromGengo($translator, 'translate/job/' . $gengo_job_id, 'PUT', $data);
  }

  /**
   * Will build and send a job to gengo service.
   *
   * @param TMGMTJob $job
   *
   * @return array
   *   Array of job objects returned from gengo.
   */
  public function gengoSendJob(TMGMTJob $job) {
    $data = tmgmt_flatten_data($job->getData());

    $translations = array();

    foreach ($data as $key => $value) {
      if (isset($value['#translate']) && $value['#translate'] === FALSE) {
        continue;
      }

      // We want to submit the job only with specific job item. Used
      // in the pseudo-reject workflow.
      if (!empty($this->singleDataItemKey) && $key != $this->singleDataItemKey) {
        continue;
      }

      $translations[$job->tjid . '][' . $key] = array(
        'type' => 'text',
        'slug' => tmgmt_data_item_label($value),
        'body_src' => $value['#text'],
        'lc_src' => $this->mapToGengoLangCode($job->source_language),
        'lc_tgt' => $this->mapToGengoLangCode($job->target_language),
        'tier' => $job->settings['quality'],
        'callback_url' => url('tmgmt_mygengo_callback', array('absolute' => TRUE)),
        'custom_data' => $job->tjid . '][' . $key,
        'auto_approve' => (int) $job->getTranslator()->getSetting('mygengo_auto_approve'),
        'use_preferred' => (int) $job->getTranslator()->getSetting('use_preferred'),
      );

      if (!empty($this->serviceComment)) {
        $translations['job' . $job->tjid . $key]['comment'] = $this->serviceComment;
      }
      // If singleDataItemKey is set and we have reached this point we are going
      // to resubmit an existing data item as a new gengo job item. Therefore
      // we need to set force flag to create a new gengo job and not to get
      // existing translation.
      if (!empty($this->singleDataItemKey)) {
        $translations['job' . $job->tjid . $key]['force'] = 1;
      }
    }

    $response = $this->requestFromGengo($job->getTranslator(), 'translate/jobs', 'POST', array(
      'jobs' => $translations,
      // In case we send one job item as group we will get error from Gengo.
      'as_group' => (int) (count($translations) > 1),
    ));

    if (isset($response->jobs)) {
      return $response->jobs;
    }

    return array();
  }

  /**
   * Maps Drupal lang code to Gengo lang code.
   *
   * @param string $drupal_code
   *
   * @return string
   *   Lang code.
   */
  protected function mapToGengoLangCode($drupal_code) {
    if (isset($this->langCodesMapping[$drupal_code])) {
      return $this->langCodesMapping[$drupal_code];
    }

    return $drupal_code;
  }
}
