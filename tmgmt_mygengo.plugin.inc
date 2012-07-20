<?php

/**
 * @file
 * Provides myGengo translation plugin controller.
 */

/**
 * myGengo translation plugin controller.
 */
class TMGMTMyGengoTranslatorPluginController extends TMGMTDefaultTranslatorPluginController implements TMGMTTranslatorRejectDataItem {

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
  protected $apiVersion = 'v1.1';

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
   * @return array
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

    /**
     *  If it's a GET or DELETE request, just sign it and send it appropriately. PUT/POST
     *  get a little more annoying...
     */
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

    if ($response->code != 200) {
      throw new TMGMTException('Unable to connect to MyGengo service due to following error: @error at @url',
        array('@error' => $response->error, '@url' => $url));
    }

    $results = json_decode($response->data);

    if ($results->opstat == 'ok' && isset($results->response)) {
      return $results->response;
    }

    // If we get to this point it is definitely an error state.
    $error_msg = isset($results->err->msg) ? $results->err->msg : 'Unknown error.';
    throw new TMGMTException('MyGengo service returned following error: @error', array('@error' => $error_msg));
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
   * Here we will acutally query source and get translations.
   */
  public function requestTranslation(TMGMTJob $job) {

    try {

      foreach ($this->gengoSendJob($job) as $response_job) {
        // The response is an array of objects, each job is wrapped in another
        // object.
        $response_job = reset($response_job);

        // If tier is "machine", API returns translation immediately.
        if ($response_job->tier == 'machine') {
          $key = array_slice(tmgmt_ensure_keys_array($response_job->custom_data), 1);
          $this->receiveTranslation($job, $key, $response_job);
        }
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
  public function receiveTranslation(TMGMTJob $job, $keys, $data) {
    if ($data->status == 'approved') {
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
      $targets = $this->requestFromGengo($translator, 'translate/service/language_pairs',
        'GET', array('lc_src' => $source_language));
      foreach ($targets as $target) {
        $results[$target->lc_tgt] = $target->lc_tgt;
      }
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e, 'Unable to retrieve a list of available languages.');
    }

    return $results;
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

      // NOTE: There's a not-so-fun-bug in the myGengo API that you'll encounter
      // if you change how this key is set. If the key in this hash starts with
      // numbers (especially a 0...), you will get an odd one-off response format
      // that can't be parsed by default.
      //
      // This is being fixed in a future iteration of the API (v2). If you encounter
      // weirdness with this issue, feel free to email us at api@mygengo.com. ;)
      //
      // - Ryan McGrath, who dislikes this error intensely
      $translations['job' . $job->tjid . $key] = array(
        'type' => 'text',
        'slug' => tmgmt_data_item_label($value),
        'body_src' => $value['#text'],
        'lc_src' => $job->source_language,
        'lc_tgt' => $job->target_language,
        'tier' => $job->settings['quality'],
        'callback_url' => url('tmgmt_mygengo_callback', array('absolute' => TRUE)),
        'custom_data' => $job->tjid . '][' . $key,
        'auto_approve' => (int) $job->getTranslator()->getSetting('mygengo_auto_approve'),
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
      'process' => 1,
      // In case we send one job item as group we will get error from Gengo.
      'as_group' => (int) (count($translations) > 1),
    ));

    return $response->jobs;
  }
}
