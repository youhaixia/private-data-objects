/* Copyright 2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "enclave_t.h"
#include "sgx_tseal.h"

#include <stdlib.h>
#include <string>
#include <vector>

#include "crypto.h"
#include "error.h"
#include "pdo_error.h"
#include "zero.h"

#include "jsonvalue.h"
#include "parson.h"

#include "enclave_data.h"

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
EnclaveData::EnclaveData(void)
{
    // do not attempt to catch errors here... let the calling procedure
    // handle the constructor errors

    // create the public verifying key
    public_signing_key_ = private_signing_key_.GetPublicKey();

    // create the public encryption key
    public_encryption_key_ = private_encryption_key_.GetPublicKey();

    SerializePrivateData();
    SerializePublicData();
}

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
EnclaveData::EnclaveData(const uint8_t* inSealedData)
{
    pdo::error::ThrowIfNull(inSealedData, "Sealed sign up data pointer is NULL");

    uint32_t decrypted_size =
        sgx_get_encrypt_txt_len(reinterpret_cast<const sgx_sealed_data_t*>(inSealedData));

    // need to check for error

    std::vector<uint8_t> decrypted_data;
    decrypted_data.resize(decrypted_size);

    // Unseal the data
    sgx_status_t ret = sgx_unseal_data(reinterpret_cast<const sgx_sealed_data_t*>(inSealedData),
        nullptr, 0, &decrypted_data[0], &decrypted_size);
    pdo::error::ThrowSgxError(ret, "Failed to unseal  data");

    std::string decrypted_data_string(reinterpret_cast<const char*>(&decrypted_data[0]));
    DeserializeSealedData(decrypted_data_string);

    SerializePrivateData();
    SerializePublicData();
}

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
void EnclaveData::DeserializeSealedData(const std::string& inSerializedEnclaveData)
{
    std::string svalue;
    const char* pvalue = nullptr;

    // Parse the incoming wait certificate
    JsonValue parsed(json_parse_string(inSerializedEnclaveData.c_str()));
    pdo::error::ThrowIfNull(parsed.value, "Failed to parse the Enclave data, badly formed JSON");

    JSON_Object* keystore_object = json_value_get_object(parsed);
    pdo::error::ThrowIfNull(keystore_object, "Failed to parse the key store object");

    // public signing key
    pvalue = json_object_dotget_string(keystore_object, "SigningKey.PublicKey");
    pdo::error::ThrowIf<pdo::error::ValueError>(
        !pvalue, "Failed to retrieve public signing key from the key store");

    svalue.assign(pvalue);
    public_signing_key_.Deserialize(svalue);

    // private signing key
    pvalue = json_object_dotget_string(keystore_object, "SigningKey.PrivateKey");
    pdo::error::ThrowIf<pdo::error::ValueError>(
        !pvalue, "Failed to retrieve private signing key from the key store");

    svalue.assign(pvalue);
    private_signing_key_.Deserialize(svalue);

    // public encryption key
    pvalue = json_object_dotget_string(keystore_object, "EncryptionKey.PublicKey");
    pdo::error::ThrowIf<pdo::error::ValueError>(
        !pvalue, "Failed to retrieve public encryption key from the key store");

    svalue.assign(pvalue);
    public_encryption_key_.Deserialize(svalue);

    // private encryption key
    pvalue = json_object_dotget_string(keystore_object, "EncryptionKey.PrivateKey");
    pdo::error::ThrowIf<pdo::error::ValueError>(
        !pvalue, "Failed to retrieve private encryption key from the key store");

    svalue.assign(pvalue);
    private_encryption_key_.Deserialize(svalue);
}

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
void EnclaveData::SerializePrivateData(void)
{
    JSON_Status jret;

    // Create serialized wait certificate
    JsonValue dataValue(json_value_init_object());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        !dataValue.value, "Failed to create enlcave data serialization object");

    JSON_Object* dataObject = json_value_get_object(dataValue);
    pdo::error::ThrowIfNull(dataObject, "Failed to retrieve serialization object");

    // private signing key
    std::string b64_private_signing_key = private_signing_key_.Serialize();
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        b64_private_signing_key.empty(), "failed to serialize the private signing key");

    jret = json_object_dotset_string(
        dataObject, "SigningKey.PrivateKey", b64_private_signing_key.c_str());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed on private signing key");

    // public signing key
    std::string b64_public_signing_key = public_signing_key_.Serialize();
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        b64_public_signing_key.empty(), "failed to serialize the public signing key");

    jret = json_object_dotset_string(
        dataObject, "SigningKey.PublicKey", b64_public_signing_key.c_str());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed on public signing key");

    // private encryption key
    std::string b64_private_encryption_key = private_encryption_key_.Serialize();
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        b64_private_encryption_key.empty(), "failed to serialize the private encryption key");

    jret = json_object_dotset_string(
        dataObject, "EncryptionKey.PrivateKey", b64_private_encryption_key.c_str());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed on private encryption key");

    // public encryption key
    std::string b64_public_encryption_key = public_encryption_key_.Serialize();
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        b64_public_encryption_key.empty(), "failed to serialize the public encryption key");

    jret = json_object_dotset_string(
        dataObject, "EncryptionKey.PublicKey", b64_public_encryption_key.c_str());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed on public encryption key");

    size_t serializedSize = json_serialization_size(dataValue);

    std::vector<char> serialized_buffer;
    serialized_buffer.resize(serializedSize + 1);

    jret = json_serialize_to_buffer(dataValue, &serialized_buffer[0], serializedSize);
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed");

    serialized_private_data_.assign(&serialized_buffer[0]);
}

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
void EnclaveData::SerializePublicData(void)
{
    JSON_Status jret;

    // Create serialized wait certificate
    JsonValue dataValue(json_value_init_object());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        !dataValue.value, "Failed to create enlcave data serialization object");

    JSON_Object* dataObject = json_value_get_object(dataValue);
    pdo::error::ThrowIfNull(dataObject, "Failed to retrieve serialization object");

    // public signing key
    std::string b64_public_signing_key = public_signing_key_.Serialize();
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        b64_public_signing_key.empty(), "failed to serialize the public signing key");

    jret = json_object_dotset_string(dataObject, "VerifyingKey", b64_public_signing_key.c_str());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed on public signing key");

    // public encryption key
    std::string b64_public_encryption_key = public_encryption_key_.Serialize();
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        b64_public_encryption_key.empty(), "failed to serialize the public encryption key");

    jret =
        json_object_dotset_string(dataObject, "EncryptionKey", b64_public_encryption_key.c_str());
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed on public encryption key");

    size_t serializedSize = json_serialization_size(dataValue);

    std::vector<char> serialized_buffer;
    serialized_buffer.resize(serializedSize + 1);

    jret = json_serialize_to_buffer(dataValue, &serialized_buffer[0], serializedSize);
    pdo::error::ThrowIf<pdo::error::RuntimeError>(
        jret != JSONSuccess, "enclave data serialization failed");

    serialized_public_data_.assign(&serialized_buffer[0]);
}
