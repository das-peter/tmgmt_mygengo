<?php

/**
 * @file
 * Provides Gengo translation plugin controller.
 */

/**
 * Gengo translation plugin controller.
 */
class TMGMTMyGengoTranslatorPluginController extends TMGMTDefaultTranslatorPluginController implements TMGMTTranslatorRejectDataItem {

  public $debug = FALSE;

  /**
   * Translation service URL.
   *
   * @var string
   */
  protected $apiURL = 'http://api.gengo.com';

  /**
   * Translation sandbox service URL.
   * @var string
   */
  protected $sandboxURL = 'http://api.sandbox.gengo.com';

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

  protected $gengoLanguagePairs = array();


  public function getDefaultRemoteLanguagesMappings() {
    return array(
      'zh-hans' => 'zh',
      'zh-hant' => 'zh-tw',
    );
  }

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

    $job_item->addMessage('Job data item %label has been resubmitted to Gengo translate service.', array('%label' => tmgmt_data_item_label($data_item)));

    return TRUE;
  }

  /**
   * Implements TMGMTTranslatorRejectDataItem::rejectForm()
   */
  public function rejectForm($form, &$form_state) {
    $form['message'] = array(
      '#markup' => '<div class="messages warning">' .
          t('By rejecting this item you will submit a new translate job to the Gengo translate service which will result in additional costs.') . '</div>',
    );
    $form['comment'] = array(
      '#type' => 'textarea',
      '#title' => t('Rejection comment'),
      '#description' => t('Provide a brief explanation that you actually rejected previous translation and state your reasons.'),
    );
    return $form;
  }

  /**
   * Builds, signs and fires a request to Gengo's API.
   *
   * Authentication rituals are documented here:
   * http://developers.gengo.com/v2/authentication/
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
        'User-Agent' => 'Drupal Gengo Translation Interface v0.1',
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
      throw new TMGMTException('Unable to connect to Gengo service due to following error: @error at @url',
        array('@error' => $response->error, '@url' => $url));
    }

    $results = json_decode($response->data);

    if ($results->opstat == 'ok' && isset($results->response)) {
      return $results->response;
    }

    // Find if we have only one error or multiple.
    if (isset($results->err->code)) {
      $gengo_err = $results->err;
    }
    // In case of multiple, take only the first one - they are usually the same.
    // TODO - handle multiple errors received from gengo.
    else {
      $gengo_err = reset($results->err);
      $gengo_err = array_shift($gengo_err);
    }

    throw new TMGMTException('<strong>#@code @error</strong>', array('@error' => $gengo_err->msg, '@code' => $gengo_err->code));
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

      $response = $this->gengoSendJob($job);

      if ($this->debug) {
        watchdog('tmgmt_mygengo', 'Job submitted. Received response: @response',
          array('@response' => var_export($response, TRUE)), WATCHDOG_DEBUG);
      }

      // If we already receive jobs, process them.
      if (!empty($response->jobs)) {
        $this->processGengoJobsUponTranslationRequest($job, $response->jobs);
      }
      elseif(isset($response->order_id)) {
        $this->initGengoMapping($job, $response->order_id);
      }

      $job->submitted(t('Job has been submitted.'));
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e);
      $job->rejected('Job has been rejected with following error: !error',
        array('!error' => $e->getMessage()), 'error');
    }
  }

  protected function processGengoJobsUponTranslationRequest(TMGMTJob $job, $response_jobs) {

    $mappings = array();

    foreach ($response_jobs as $key => $response_job) {

      // Duplicate content has been submitted.
      if (isset($response_job->duplicate)) {
        // TODO - we need to handle duplicates in some way as client will not
        // receive translation for this item.
        continue;
      }

      // For machine translations the job is not wrapped in another object
      // however for human translations it is. So try to cope with this
      // gengo system variety.
      if (!isset($response_job->custom_data)) {
        $response_job = reset($response_job);
      }

      // In case we receive an existing translation the array offset IS also
      // the data kay, and we ought to use it as the object custom data is not
      // updated. However this is not always the case and in some case we
      // receive numeric keys, so try to set some custom data to increase the
      // chance of matching the job.
      if (is_numeric($key)) {
        $key = $response_job->custom_data;
      }

      $key = array_slice(tmgmt_ensure_keys_array($key), 1);
      $this->saveTranslation($job, $key, $response_job);

      $mappings[] = array(
        'tjid' => $job->tjid,
        // Yes, this is not a joke, they really return string value "NULL" in
        // case of a machine translation.
        'gjid' => $response_job->job_id == 'NULL' ? 0 : $response_job->job_id,
        'data_item_key' => tmgmt_ensure_keys_string($key),
      );
    }

    // For jobs received upon submission we create gengo mappings differently
    // than in case of regular translation cycle.
    $this->saveGengoMappings($mappings);
  }

  /**
   * Receives and stores a translation returned by Gengo.
   */
  public function saveTranslation(TMGMTJob $job, $keys, $data) {
    if ($data->status == 'approved') {
      $keys = tmgmt_ensure_keys_array($keys);
      $job->addTranslatedData(array('#text' => $data->body_tgt), $keys);

      // Look for duplicated strings that were saved with a mapping to this key.
      // @todo: Allow additional conditions in TmgmtJob::getRemoteMappings()?
      $query = new EntityFieldQuery();
      $query->entityCondition('entity_type', 'tmgmt_remote');
      $query->propertyCondition('tjid', $job->tjid);
      $query->propertyCondition('remote_identifier_1', tmgmt_ensure_keys_string($keys));

      $result = $query->execute();

      if (isset($result['tmgmt_remote'])) {
        $remote_mappings = entity_load('tmgmt_remote', array_keys($result['tmgmt_remote']));
        // If we found any mappings, also add the translation for those.
        foreach ($remote_mappings as $remote_mapping) {
          $job->addTranslatedData(array('#text' => $data->body_tgt), $remote_mapping->data_item_key);
        }
      }
    }
  }

  /**
   * Implements TMGMTDefaultTranslatorPluginController::getSupportedRemoteLanguages().
   */
  function getSupportedRemoteLanguages(TMGMTTranslator $translator) {
    if (!empty($this->supportedRemoteLanguages)) {
      return $this->supportedRemoteLanguages;
    }

    try {
      $languages = $this->requestFromGengo($translator, 'translate/service/languages', 'GET');
      foreach($languages as $gengo_language) {
        $this->supportedRemoteLanguages[$gengo_language->lc] = $gengo_language->lc;
      }
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e);
    }

    return $this->supportedRemoteLanguages;
  }

  /**
   * Gets gengo language pairs.
   *
   * @param TMGMTTranslator $translator
   * @param $source_language
   * @param $target_language
   *
   * @return array
   */
  public function getGengoLanguagePairs(TMGMTTranslator $translator, $source_language, $target_language = NULL) {
    try {
      if (empty($this->gengoLanguagePairs[$source_language])) {
        $this->gengoLanguagePairs[$source_language] = $this->requestFromGengo($translator, 'translate/service/language_pairs',
          'GET', array('lc_src' => $this->mapToRemoteLanguage($translator, $source_language)));
      }
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e);
    }

    // If target language provided, filter out all other langs.
    if (!empty($target_language)) {
      $lang_pairs = array();

      foreach ($this->gengoLanguagePairs[$source_language] as $lang_pair) {
        if ($lang_pair->lc_tgt == $this->mapToRemoteLanguage($translator, $target_language)) {
          $lang_pairs[] = $lang_pair;
        }
      }

      return $lang_pairs;
    }

    return $this->gengoLanguagePairs[$source_language];
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::getSupportedTargetLanguages().
   */
  public function getSupportedTargetLanguages(TMGMTTranslator $translator, $source_language) {
    $results = array();

    foreach ($this->getGengoLanguagePairs($translator, $source_language) as $target) {
      $results[$target->lc_tgt] = $target->lc_tgt;
    }

    return $results;
  }

  /**
   * Get a quote from Gengo for the given job.
   *
   * @param TMGMTJob $job
   *
   * @return array
   *   Array with the following keys: currency, estimates, highest_eta,
   *   sum_credits, sum_eta, sum_word_count, and unit_price.
   * @throws TMGMTException
   *   In case of error doing request to gengo service.
   */
  public function getQuote(TMGMTJob $job) {
    $response = $this->gengoSendJob($job, TRUE);

    if ($this->debug) {
      watchdog('tmgmt_mygengo', 'Job Quote submitted. Received response: @response',
        array('@response' => var_export($response, TRUE)), WATCHDOG_DEBUG);
    }

    // Setup empty values
    $quote = array(
      'currency' => '',
      'estimated' => FALSE,
      'highest_eta' => 0,
      'sum_credits' => 0,
      'sum_word_count' => 0,
    );

    if (!empty($response->jobs)) {
      $jobs = (array) $response->jobs;

      $quote['currency'] = reset($jobs)->currency;

      // Sum up quotes from each job.
      foreach ($response->jobs as $job) {
        $quote['sum_word_count'] += $job->unit_count;
        $quote['sum_credits'] += $job->credits;

        if ($job->eta > $quote['highest_eta']) {
          $quote['highest_eta'] = $job->eta;
        }
      }
    }

    return $quote;
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

    $target_lang = $this->mapToRemoteLanguage($translator, $job->target_language);
    $source_lang = $this->mapToRemoteLanguage($translator, $job->source_language);

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
   * @param boolean $quote_only
   *   (Optional) Set to TRUE to only get a quote for the given job.
   *
   * @return mixed
   *   - Array of job objects returned from gengo.
   *   - Status object with order info.
   */
  public function gengoSendJob(TMGMTJob $job, $quote_only = FALSE) {
    $data = tmgmt_flatten_data($job->getData());

    $translations = array();

    $sources = array();

    foreach ($data as $key => $value) {
      if (isset($value['#translate']) && $value['#translate'] === FALSE) {
        continue;
      }

      // We want to submit the job only with specific job item. Used
      // in the pseudo-reject workflow.
      if (!empty($this->singleDataItemKey) && $key != $this->singleDataItemKey) {
        continue;
      }

      if (!$quote_only) {
        // Detect duplicate source strings and add a mapping for them.
        if ($duplicate_key = array_search($value['#text'], $sources)) {

          // Only add the actual mapping in case we are not requesting a quote.
          if (!$quote_only) {
            // Get the job item.
            // @todo: This is not very nice. We should possibly support this
            //   directly on the job.
            $tjiid = substr($key, 0, strpos($key, ']'));
            $job_item = tmgmt_job_item_load($tjiid);
            $job_item->addRemoteMapping($key, $duplicate_key);
          }
          continue;
        }
      }

      // Keep track of source texts for easy lookup.
      $sources[$key] = $value['#text'];

      $translations[$job->tjid . '][' . $key] = array(
        'type' => 'text',
        'slug' => tmgmt_data_item_label($value),
        'body_src' => $value['#text'],
        'lc_src' => $this->mapToRemoteLanguage($job->getTranslator(), $job->source_language),
        'lc_tgt' => $this->mapToRemoteLanguage($job->getTranslator(), $job->target_language),
        'tier' => $job->settings['quality'],
        'callback_url' => url('tmgmt_mygengo_callback', array('absolute' => TRUE)),
        'custom_data' => $job->tjid . '][' . $key,
        'auto_approve' => (int) $job->getTranslator()->getSetting('mygengo_auto_approve'),
        'use_preferred' => (int) $job->getTranslator()->getSetting('use_preferred'),
      );

      if (!empty($this->serviceComment)) {
        $translations[$job->tjid . '][' . $key]['comment'] = $this->serviceComment;
      }
      // If singleDataItemKey is set and we have reached this point we are going
      // to resubmit an existing data item as a new gengo job item. Therefore
      // we need to set force flag to create a new gengo job and not to get
      // existing translation.
      if (!empty($this->singleDataItemKey)) {
        $translations[$job->tjid . '][' . $key]['force'] = 1;
      }
    }

    $url = $quote_only ? 'translate/service/quote' : 'translate/jobs';
    return $this->requestFromGengo($job->getTranslator(), $url, 'POST', array(
      'jobs' => $translations,
      // In case we send one job item as group we will get error from Gengo.
      'as_group' => (int) (count($translations) > 1),
    ));
  }

  /**
   * Saves mapping with tmgmt data item and gengo job.
   *
   * @param array $mappings
   *   Array of associated mappings data to save in format:
   *   - tjid - Translation job id
   *   - gjid - Gengo translation job id
   *   - key - Data item key
   */
  public function saveGengoMappings(array $mappings) {
    foreach ($mappings as $mapping) {
      $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m', array('tjid'));
      $select->condition('tjid', $mapping['tjid']);
      $select->condition('gjid', $mapping['gjid']);

      if ($select->execute()->fetchField()) {
        drupal_write_record('tmgmt_mygengo_mappings', $mapping, array('tjid', 'gjid'));
      }
      else {
        drupal_write_record('tmgmt_mygengo_mappings', $mapping);
      }
    }
  }

  /**
   * Creates a placeholder record in the mapping table.
   *
   * The idea here is not to introduce additional storage to temporarily store
   * gegngo order id before we get gengo job ids.
   *
   * @param TMGMTJob $job
   * @param int $gorder_id
   *   Gengo job id.
   */
  protected function initGengoMapping(TMGMTJob $job, $gorder_id) {
    $data = array(
      'tjid' => $job->tjid,
      'gorder_id' => $gorder_id,
    );
    drupal_write_record('tmgmt_mygengo_mappings', $data);
  }

  /**
   * Maps TMGMT job data items to gengo jobs.
   *
   * @param TMGMTJob $job
   */
  public function mapGengoJobs(TMGMTJob $job) {

    // Search for placeholder item.
    $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m', array('gorder_id'));
    $select->condition('tjid', $job->tjid);
    $select->condition('gjid', '');

    // If we have a placeholder we want to fetch gengo job ids.
    if ($gorder_id = $select->execute()->fetchField()) {
      $request = $this->requestFromGengo($job->getTranslator(), 'translate/order/' . $gorder_id, 'GET');

      if ($this->debug) {
        watchdog('tmgmt_mygengo', 'Fetching order info. Received response: @response',
          array('@response' => var_export($request, TRUE)), WATCHDOG_DEBUG);
      }

      // If all the jobs are not yet in Gengo's system, do nothing.
      if($request->order->jobs_queued != 0){
        return;
      }

      $gengo_job_ids = array();
      if (!empty($request->order->jobs_available)) {
        $gengo_job_ids += $request->order->jobs_available;
      }
      if (!empty($request->order->jobs_pending)) {
        $gengo_job_ids += $request->order->jobs_pending;
      }
      if (!empty($request->order->jobs_reviewable)) {
        $gengo_job_ids += $request->order->jobs_reviewable;
      }
      if (!empty($request->order->jobs_approved)) {
        $gengo_job_ids += $request->order->jobs_approved;
      }

      // Remove the placeholder record and insert actual
      db_delete('tmgmt_mygengo_mappings')->condition('gorder_id', $gorder_id)->execute();

      $mappings = array();
      foreach ($gengo_job_ids as $gengo_job_id) {
        $mappings[] = array(
          'tjid' => $job->tjid,
          'gjid' => $gengo_job_id,
          'gorder_id' => $gorder_id,
        );
      }
      // Add intial records into the table
      $this->saveGengoMappings($mappings);
    }

    // Now load all Gengo job ids and try to get mapping data.
    $gjids = $this->getGengoJobIds($job->tjid);
    $response = NULL;

    // We have fetch mapping data.
    $response = $this->requestFromGengo($job->getTranslator(),
        'translate/jobs/' . implode(',', $gjids), 'GET');

    if ($this->debug) {
      watchdog('tmgmt_mygengo', 'Mapping gengo jobs. tjid: @tjid gjids: @gjids. Received response: @response',
        array(
          '@tjid' => $job->tjid,
          '@gjids' => implode(', ', $gjids),
          '@response' => var_export($response, TRUE)
        ), WATCHDOG_DEBUG);
    }

    // Gengo did not provide any mapping data, do nothing.
    // This only happens in the case that Gengo is unreachable.
    if (empty($response->jobs)) {

      if ($this->debug) {
        watchdog('tmgmt_mygengo', 'Mapping gengo jobs. tjid: @tjid gjids: @gjids. Received response: @response',
          array(
            '@tjid' => $job->tjid,
            '@gjids' => implode(', ', $gjids),
            '@response' => var_export($response, TRUE)
          ), WATCHDOG_DEBUG);
      }
      return;
    }

    // update mappings and save any translations
    $mappings = array();
    foreach ($response->jobs as $key => $response_job) {
      $mappings[] = array(
        'tjid' => $job->tjid,
        'gjid' => $response_job->job_id,
        'data_item_key' => $response_job->custom_data,
      );

      if (is_numeric($key)) {
        $key = $response_job->custom_data;
      }
      $key = array_slice(tmgmt_ensure_keys_array($key), 1);
      $this->saveTranslation($job, $key, $response_job);
    }
    // Update mapping records
    $this->saveGengoMappings($mappings);
  }

  /**
   * Gets gengo job ids for a tmgmt job.
   *
   * @param int $tjid
   *   TMGMT Job id.
   *
   * @return array
   *   Array of gengo job ids.
   */
  public function getGengoJobIds($tjid) {
    $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m', array('gjid'));
    $select->condition('tjid', $tjid);

    return $select->execute()->fetchCol();
  }

  /**
   * Gets gengo mappings not being mapped to local data items.
   *
   * @param int $tjid
   * @return array
   *   Array of mapping records keyd by gengo job ids.
   */
  public function getMappedGengoData($tjid) {
    $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m');
    $select->condition('tjid', $tjid);
    $select->condition('data_item_key', '', '!=');

    return $select->execute()->fetchAllAssoc('gjid');
  }
}
